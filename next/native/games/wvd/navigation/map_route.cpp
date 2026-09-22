#include "map_route.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::navigation {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
tasks::CompiledWorkflow reach_map_target(const MapTarget &target,
                                         const std::optional<std::string> &floor) {
    if (target.swipes.empty() || target.swipes.size() > 32)
        throw std::runtime_error("MAP_SEARCH_BUDGET_INVALID");
    if (target.target == "stay" || target.target == "dungFlag")
        throw std::runtime_error("MAP_TARGET_REQUIRES_AUTO_ROUTE");
    C graph("navigation.map_target");
    const auto map = C::image("mapFlag"), dungeon = C::image("dungFlag");
    const J combat{{"mode", "combat_active"}};
    const auto chest = C::any({C::image("chestFlag"), C::image("whowillopenit"), C::image("chestOpening")});
    const auto encounter = C::any({combat, chest});
    const auto map_scene = C::all({map, C::absent(encounter)});
    const auto moving = C::all({dungeon, C::absent(map), C::absent(encounter)});
    auto correct_map = map_scene;
    if (floor)
        correct_map = C::all({map_scene, C::image(*floor)});
    auto target_match = C::image(target.target);
    if (!target.regions.empty()) {
        target_match["roi"] = target.regions.front();
        target_match["exclude"] = J::array();
        for (std::size_t i = 1; i < target.regions.size(); ++i)
            target_match["exclude"].push_back(target.regions[i]);
    }
    bool positional = target.target == "position" || target.target.starts_with("stair");
    const bool exit_target = target.target == "harken" || target.target == "Bharken" ||
                             target.target == "leaveDung" || target.target.ends_with("_quit");
    const auto outside = C::all({C::any({vision::city_screen(), C::image("returnText"),
                                        C::image("returntoTown"), C::image("openworldmap"), C::image("worldmapflag")}),
                                 C::absent(map), C::absent(encounter)});
    J done;
    if (positional) {
        if (!target.position)
            throw std::runtime_error("MAP_TARGET_POSITION_REQUIRED");
        done = {{"mode", target.target == "position" ? "reached" : "through_stair"},
                {"position", *target.position}};
        if (target.target != "position")
            done["image"] = target.target;
    } else if (target.target != "chest" && !exit_target) {
        auto focus = target_match;
        focus["mode"] = "focus_cursor";
        done = C::all({target_match, C::absent(focus)});
    }
    if (target.hint == MapTarget::Hint::StairReference) {
        target_match["mode"] = "harken_stair";
        target_match["stair"] = target.stair_reference;
    }
    J interrupted = {"Encounter"};
    if (floor)
        interrupted.push_back("WrongFloor");
    auto entry = interrupted;
    entry.push_back("BeginSearch");
    entry.push_back("OpenMap");
    graph.route("Entry", entry);
    graph.observe("Encounter", encounter, {"EncounterExit"});
    graph.recovery("EncounterExit", "navigation.encounter_requires_dispatch");
    if (floor) {
        graph.observe("WrongFloor", C::all({map_scene, C::absent(C::image(*floor))}), {"FloorExit"});
        graph.recovery("FloorExit", "navigation.wrong_floor");
    }
    graph.observe("BeginSearch", correct_map, {"Search0"});
    graph.fixed_click("OpenMap", moving, C::any({map, encounter}), {777, 150}, {"Entry"});
    const auto search_count = positional ? std::size_t{1} : target.swipes.size();
    for (std::size_t i = 0; i < search_count; ++i) {
        const auto n = std::to_string(i);
        const auto search = "Search" + n;
        const auto choose = "Choose" + n;
        const auto selected = "Select" + n;
        J candidates = interrupted;
        if (target.hint == MapTarget::Hint::StairReference)
            candidates.push_back("WrongStair" + n);
        if (!done.is_null())
            candidates.push_back("Arrived" + n);
        candidates.push_back(selected);
        if (!positional)
            candidates.push_back("Miss" + n);
        if (target.swipes[i]) {
            const auto &s = *target.swipes[i];
            graph.swipe(search, correct_map, C::any({map, encounter}),
                        {s.from[0], s.from[1], s.to[0], s.to[1]}, {choose});
            graph.delay_after(search, 2000);
        } else
            graph.route(search, {choose});
        graph.route(choose, candidates);
        if (!done.is_null())
            graph.observe("Arrived" + n, C::all({correct_map, done}), {"Terminal"});
        auto after_select = interrupted;
        after_select.push_back("AutoMove");
        if (positional)
            graph.fixed_click(selected, C::all({correct_map, C::absent(done)}),
                              C::any({map, encounter}), *target.position,
                              after_select);
        else
            graph.click(selected, correct_map, target_match, C::any({map, encounter}),
                        after_select);
        // 全部搜索方向均无宝箱才算宝箱目标结束；其他目标无匹配不冒充到达。
        const J next = i + 1 < search_count ? J{"Search" + std::to_string(i + 1)}
                         : target.target == "chest" ? J{"Terminal"} : J{"MissingExit"};
        if (!positional)
            graph.observe("Miss" + n, C::all({correct_map, C::absent(target_match)}), next);
        if (target.hint == MapTarget::Hint::StairReference) {
            const auto fallback = "WrongStair" + n;
            // 旧 CheckIf_harkenStair 在本任务的拖图之后，先完整搜索 harken，
            // 全部未命中才搜索 Bharken；内层不再检查原 stair，也不复用原 ROI。
            graph.observe(fallback, C::all({correct_map, C::absent(C::image(target.stair_reference))}),
                          {fallback + "harkenSearch0"});
            graph.hit_limit(fallback, 5);
            constexpr std::array<std::array<int, 4>, 5> swipes{{
                {100, 100, 700, 1200}, {400, 1200, 400, 100}, {700, 800, 100, 800},
                {400, 100, 400, 1200}, {100, 800, 700, 800}}};
            for (const auto *symbol : {"harken", "Bharken"}) {
                const auto prefix = fallback + symbol;
                const auto image = C::image(symbol);
                for (std::size_t view = 0; view <= swipes.size(); ++view) {
                    const auto suffix = std::to_string(view);
                    const auto scan = prefix + "Search" + suffix;
                    const auto probe = prefix + "Choose" + suffix;
                    const auto select = prefix + "Select" + suffix;
                    const auto miss = prefix + "Miss" + suffix;
                    if (view == 0)
                        graph.route(scan, {probe});
                    else {
                        graph.swipe(scan, correct_map, C::any({map, encounter}), swipes[view - 1], {probe});
                        graph.delay_after(scan, 2000);
                    }
                    auto choices = interrupted;
                    choices.push_back(select);
                    choices.push_back(miss);
                    graph.route(probe, choices);
                    graph.click(select, correct_map, image, C::any({map, encounter}), after_select);
                    const J following = view < swipes.size() ? J{prefix + "Search" + std::to_string(view + 1)}
                        : std::string(symbol) == "harken" ? J{fallback + "BharkenSearch0"} : next;
                    graph.observe(miss, C::all({correct_map, C::absent(image)}), following);
                    // 固定六视图、每节点至多五次；既有 Session/移动预算保持不变。
                    for (const auto &node : {scan, probe, select, miss})
                        graph.hit_limit(node, 5);
                }
            }
        }
    }
    if (!positional && target.target != "chest")
        graph.recovery("MissingExit", "navigation.target_missing");
    // 坐标和普通资源目标也可能跨越副本出口。退场由新画面证明，不按目标名称猜测；
    // 父路线先检查 Outside，不能把提前离开误计为当前坐标已到达。
    J after_move = {"Exited", "Encounter", "Frozen", "CloseStaleMap", "Moving"};
    graph.observe("Exited", outside, {"Terminal"});
    graph.fixed_click("AutoMove", correct_map,
                      {{"mode", "map_route_post"}},
                      {136, 1431}, {"WaitAfterMove"});
    graph.route("WaitAfterMove", after_move);
    graph.delay_after("AutoMove", 3000);
    auto hint = C::image("AutoMove");
    hint.update({{"roi", {120, 250, 780, 950}}, {"threshold", 0.35}});
    graph.observe("Frozen", C::all({map_scene, hint}), {"FrozenExit"});
    graph.recovery("FrozenExit", "navigation.automove_physics_frozen");
    graph.back("CloseStaleMap", C::all({map_scene, C::absent(hint)}),
               C::any({moving, encounter}), {"Encounter", "Moving"});
    J during_move = {"Exited", "Encounter", "Stopped", "Moving"};
    graph.observe("Moving", moving, during_move);
    graph.observe("Stopped", C::all({moving, J{{"mode", "movement_stopped"}}}), {"OpenMap"});
    // 多次采样不产生输入；时间和节点数均有界，预算耗尽交给恢复。
    graph.hit_limit("Moving", 100);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "navigation.common_screen_requires_dispatch");
    return graph.finish();
}
}
