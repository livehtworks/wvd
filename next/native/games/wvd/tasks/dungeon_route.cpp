#include "dungeon_route.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/wall_bypass.hpp"
#include "games/wvd/supply/dungeon_recover.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/recovery/revival.hpp"
#include "games/wvd/vision/harken_probes.hpp"
#include "games/wvd/vision/boot_probes.hpp"

namespace wvd::games::tasks {
using J = nlohmann::json;
using C = PipelineCompiler;
namespace {
bool automatic_target(const MapTarget &target) {
    return target.target == "stay" || target.target == "chest_auto" ||
           target.target == "mark_auto" || target.target == "dungFlag";
}
// 子图完成只是到达确认入口；仍用新帧复核任务点，而非按已发送的动作计数。
J point_confirmation(const MapTarget &target, const J &map) {
    if (automatic_target(target)) {
        if (target.harken_arrival)
            return vision::harken_floor_menu();
        const auto unavailable = C::any({C::image("NoChestCanBeFound"), C::image("theRouteToTheDestinationCannotBeFound")});
        if (target.target == "mark_auto" || target.target == "chest_auto") {
            auto focus = C::image(target.target);
            focus["mode"] = "focus_cursor";
            return C::any({unavailable, C::all({map, C::image(target.target), C::absent(focus)})});
        }
        return unavailable;
    }
    if (target.position) {
        J reached{{"mode", target.target == "position" ? "reached" : "through_stair"}, {"position", *target.position}};
        if (target.target != "position")
            reached["image"] = target.target;
        const auto on_map = C::all({map, reached});
        return target.harken_arrival ? C::any({on_map, vision::harken_floor_menu()}) : on_map;
    }
    auto image = C::image(target.target);
    if (!target.regions.empty()) {
        image["roi"] = target.regions.front();
        image["exclude"] = J::array();
        for (std::size_t i = 1; i < target.regions.size(); ++i)
            image["exclude"].push_back(target.regions[i]);
    }
    if (target.target == "chest")
        return C::all({map, C::absent(image)});
    auto focus = image;
    focus["mode"] = "focus_cursor";
    return C::all({map, image, C::absent(focus)});
}
}
std::string dungeon_task_stop_image(DungeonTaskStop stop) {
    switch (stop) {
    case DungeonTaskStop::None: return "";
    case DungeonTaskStop::CaveEna: return "COS/EnaTheAdventurer";
    case DungeonTaskStop::CaveRequest: return "COS/requestwasfor";
    }
    throw std::runtime_error("DUNGEON_TASK_STOP_INVALID");
}
CompiledWorkflow traverse_dungeon(const WvdTaskPlan &plan, const J &profile,
                                 const std::set<std::string> &available_images, bool allow_download,
                                 recovery::DialoguePolicy dialogue, DungeonTaskStop task_stop) {
    const auto stop_image = dungeon_task_stop_image(task_stop);
    const bool stopping = !stop_image.empty();
    const auto policy_stops = recovery::dialogue_task_stops(dialogue);
    if ((stopping && (policy_stops.size() != 1 || policy_stops.front() != stop_image)) ||
        (!stopping && !policy_stops.empty()))
        throw std::runtime_error("DUNGEON_TASK_STOP_POLICY_MISMATCH");
    const auto stop = stopping ? C::image(stop_image) : J{};
    const auto candidates = [&](J next) {
        if (stopping) next.insert(next.begin(), "TaskStop");
        return next;
    };
    if (plan.route().empty() || plan.route().size() > 64)
        throw std::runtime_error("DUNGEON_ROUTE_SIZE_INVALID");
    const auto chest_workflow = wvd::games::chest::open_chest(profile.at("WHO_WILL_OPEN_IT").get<int>(),
        profile.at("QUICK_DISARM_CHEST").get<bool>(), 0);
    // 父段需容纳完整的一次有界开箱，再保留路线/遭遇的原预算；旧 400 秒
    // 是无进展窗口，不能直接拿它截断可达 900 秒的有限子链。不是放宽帧 TTL。
    C graph("tasks.dungeon_route." + plan.definition().id, std::chrono::seconds{400} + chest_workflow.time_limit);
    graph.check_policy("navigation", {"wvd-network-retry", "wvd-pause", "wvd-download", "wvd-story", "wvd-blessing", "wvd-karma", "wvd-party-death", "wvd-party-defeat", "wvd-dialogue", "wvd-special-dialogue", "wvd-sandman"});
    if (!allow_download) {
        // 局部禁下载仍覆盖根策略；不因删除全量Common扫描而丢失原权限。
        const auto deny = graph.define_child("DownloadPermission", recovery::handle_download_prompt(false));
        graph.event_scope("Entry", J::array({J{{"id", "wvd-download"}, {"class", "exception"},
            {"priority", 900}, {"source_node", "Entry"}, {"entry", deny},
            {"detect", C::any({vision::download_button_zh_hant(), vision::download_button_en()})},
            {"resume", {{"mode", "reobserve"}}}}}));
    }
    const J combat{{"mode", "combat_active"}};
    auto reward = C::image("chest_reward_advance");
    reward["roi"] = {730, 1330, 170, 270};
    const auto chest = C::any({C::image("chestFlag"), C::image("whowillopenit"), C::image("chestOpening"), reward});
    const auto revive = C::image("RiseAgain");
    const auto encounter = C::any({combat, chest, revive});
    const J input_clear{{"mode", "input_clear"}, {"phase", "navigation"}};
    const auto map = C::all({C::image("mapFlag"), C::absent(encounter), input_clear});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(C::image("mapFlag")),
                                C::absent(encounter), C::absent(C::image("trait")), C::absent(C::image("recover")), input_clear});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("returntoTown"), C::image("returnText"),
                                        C::image("openworldmap"), C::image("worldmapflag")}),
                                 C::absent(C::image("mapFlag")), C::absent(encounter)});
    const auto inside = C::all({C::any({map, dungeon, encounter}), input_clear});
    graph.route("Entry", candidates({"UnknownFrozen", "Outside", "Entered", "UnknownLeap", "UnknownTimeout", "UnknownLimit", "UnknownWait"}));
    if (stopping) {
        graph.observe("TaskStop", stop, {"Terminal"});
    }
    graph.observe("UnknownFrozen", {{"mode", "unknown_frozen"}, {"classification", "scope_exhausted"}}, {"UnknownFrozenExit"});
    graph.recovery("UnknownFrozenExit", "dungeon.unknown_static_window");
    graph.unknown_leap("UnknownLeap", {"UnknownLeapExit"}, J::array(), true);
    graph.recovery("UnknownLeapExit", "leap.unknown");
    graph.observe("UnknownTimeout", C::business("/encounter_timed_out", true), {"UnknownTimeoutExit"});
    graph.recovery("UnknownTimeoutExit", "dungeon.encounter_timeout");
    graph.observe("UnknownLimit", {{"mode", "unknown_exhausted"}, {"classification", "scope_exhausted"}, {"max_tries", profile.at("MAX_TRY_LIMIT")}}, {"UnknownLimitExit"});
    graph.recovery("UnknownLimitExit", "dungeon.unknown_try_limit");
    // 所有已知候选均失败才等待；不在未知页点返回/1,1，也不让三秒候选超时替代十帧窗口。
    graph.poll("UnknownWait", 1000, {"Entry"});
    graph.hit_limit("UnknownWait", 128);
    graph.confirm("Entered", "dungeon.enter", "dungeon_entered", inside, {"Dispatch"});
    J dispatch = {"UnknownFrozen"};
    if (plan.route().back().harken_arrival) dispatch.push_back("HarkenCompleted");
    for (const auto *name : {"Blocked", "Combat", "Chest", "Revive", "Outside", "HealingPanel", "Resume", "Map"})
        dispatch.push_back(name);
    if (plan.route().back().harken_arrival) dispatch.push_back("HarkenArrived");
    for (const auto *name : {"UnknownLeap", "UnknownTimeout", "UnknownLimit", "UnknownWait"})
        dispatch.push_back(name);
    graph.route("Dispatch", candidates(std::move(dispatch)));
    if (plan.route().back().harken_arrival) {
        // 最后一个地图点已由确认事件写入业务进度后，楼层菜单就是本段终点；
        // 不再回到需要迷宫/地图图标的 Dispatch 中等待到超时。
        graph.observe("HarkenCompleted", C::all({vision::harken_floor_menu(),
            C::business("/task_step", plan.route().size())}), {"Terminal"});
        // 地图子图可能先被随机加护打断；返回楼层菜单后仍需结合最后任务点
        // 的业务进度确认到达，不能把任意哈肯菜单直接算作完成。
        graph.observe("HarkenArrived", C::all({vision::harken_floor_menu(),
            C::business("/task_step", plan.route().size() - 1)}),
            {"Confirm" + std::to_string(plan.route().size() - 1)});
    }
    graph.observe("Blocked", C::absent(input_clear), {"ClearBlocking"});
    // 具体事件由已声明的处理器接管，没有对应处理器时保留明确失败。
    graph.recovery("ClearBlocking", "dungeon.local_overlay_unhandled");
    graph.diagnostic_candidate("Blocked");
    graph.hit_limit("Blocked", 32);
    graph.hit_limit("ClearBlocking", 32);
    graph.observe("Outside", outside, {"Terminal"});
    const auto resurrection = graph.define_child("Resurrection", recovery::revive_after_defeat(), {"BlockedExit"});
    graph.observe("Revive", revive, {"Resurrect"});
    graph.hit_limit("Revive", 32);
    graph.call_child("Resurrect", resurrection, {"Dispatch"});
    graph.hit_limit("Resurrect", 32);

    const auto battle = graph.define_child("Battle", wvd::games::combat::fight_encounter(profile, available_images));
    graph.observe("Combat", combat, {"Fight"});
    graph.call_child("Fight", battle, {"Dispatch"}, {{"blocked", {"Dispatch"}}, {"revive", {"Dispatch"}}, {"chest", {"Dispatch"}}});
    const auto box = graph.define_child("Box", chest_workflow);
    graph.observe("Chest", chest, {"OpenChest"});
    graph.call_child("OpenChest", box, {"Dispatch"}, {{"combat", {"Dispatch"}}, {"revive", {"Dispatch"}}, {"ambush", {"Dispatch"}}, {"blocked", {"Dispatch"}}, {"retry", {"Dispatch"}}});

    const auto heal = graph.define_child("Heal", supply::recover_in_dungeon());
    graph.observe("HealingPanel", C::all({C::any({C::image("trait"), C::image("recover")}),
        C::absent(encounter), C::business("/healing_required", true)}), {"Heal"});
    graph.confirm("Resume", "dungeon.resume", "dungeon_resumed", dungeon, {"Heal"});
    const bool bypass = profile.at("BYPASS_THE_WALL").get<bool>() && plan.definition().type == "dungeon";
    graph.call_child("Heal", heal, candidates({bypass ? "BypassWall" : "SelectPoint"}), {{"encounter", {"Dispatch"}}, {"blocked", {"Dispatch"}}});
    if (bypass) {
        const auto wall = graph.define_child("Wall", navigation::bypass_wall_after_restart(), {"InterruptedExit"});
        graph.call_child("BypassWall", wall, {"Blocked", "Combat", "Chest", "Revive", "Outside", "HealingPanel", "SelectPoint"});
    }
    // 与旧 StateDungeon 一致，仅 Dungeon 分支调度角色恢复；已打开地图时不新增关闭地图动作。
    graph.observe("Map", map, {"SelectPoint"});

    J points = candidates({"Blocked", "Combat", "Chest", "Revive", "Outside", "Finished"});
    for (std::size_t i = 0; i < plan.route().size(); ++i)
        points.push_back("Point" + std::to_string(i));
    graph.route("SelectPoint", points);
    graph.observe_business("Finished", C::business("/task_step", plan.route().size()), {"Terminal"});
    for (std::size_t i = 0; i < plan.route().size(); ++i) {
        const auto &target = plan.route()[i];
        const auto suffix = std::to_string(i);
        const bool automatic = automatic_target(target);
        auto child = automatic ? navigation::auto_route(target.target)
                               : navigation::reach_map_target(target, plan.floor());
        J normal{{"encounter", {"Dispatch"}}, {"blocked", {"Dispatch"}}};
        if (automatic) {
            normal["stopped"] = {"Dispatch"};
            if (target.target == "mark_auto" || target.target == "chest_auto") {
                // 旧 startAuto 在停止后仍会打开地图并执行 StateMapSearch。
                // 不能只回 Dispatch 再点同一自动按钮，否则到达标记后永远不推进。
                J exits{{"encounter", {"Dispatch"}}, {"blocked", {"Dispatch"}}};
                if (plan.floor())
                    exits["floor"] = {"Retreat"};
                const auto map_definition = graph.define_child("AutoMap" + suffix, navigation::reach_map_target(target, plan.floor()));
                const auto map_entry = "CallAutoMap" + suffix;
                graph.call_child(map_entry, map_definition, candidates({"Outside", "Confirm" + suffix}), exits);
                normal["stopped"] = {map_entry};
                if (target.target == "chest_auto")
                    normal["unavailable"] = {map_entry};
            }
        } else if (plan.floor())
            normal["floor"] = {"Retreat"};
        const auto route_definition = graph.define_child("Route" + suffix, child);
        const auto route = "CallRoute" + suffix;
        graph.call_child(route, route_definition, candidates({"Outside", "Confirm" + suffix}), normal);
        graph.observe_business("Point" + suffix, C::business("/task_step", i), {route});
        graph.confirm("Confirm" + suffix, "point." + suffix, "target_completed",
            // 确认动作另取新帧。基础弹窗探针沿用已有并行实现，避免串行扫描耗尽
            // 观察有效期；仍检查全部原有弹窗，不复用子图的旧帧或放宽TTL。
            C::all({J{{"mode", "input_clear"}}, point_confirmation(target, map)}), {"Dispatch"}, i);
    }
    if (plan.floor()) {
        const auto retreat = graph.define_child("WrongFloor", navigation::auto_route("dungFlag"));
        graph.call_child("Retreat", retreat, {"Outside", "Dispatch"},
            {{"encounter", {"Dispatch"}}, {"stopped", {"Dispatch"}}, {"blocked", {"Dispatch"}}});
    }
    // 正面业务先分类；未知窗口和重启/转交判据只能在当前业务及事件均不符后检查。
    // 本次不削弱 UnknownWindow 的旧判据，只改变它被调用的位置。
    for (const auto *name : {"UnknownFrozen", "UnknownLeap", "UnknownTimeout", "UnknownLimit"})
        graph.diagnostic_candidate(name);
    // 分类成功显式打断连续未知窗口，避免挪到末尾后遗留上一段未知历史。
    for (const auto *name : {"Entered", "Outside", "Combat", "Chest", "Revive",
                             "HealingPanel", "Resume", "Map"})
        graph.mark_known_scene(name);
    if (stopping) graph.mark_known_scene("TaskStop");
    if (plan.route().back().harken_arrival) {
        graph.mark_known_scene("HarkenCompleted");
        graph.mark_known_scene("HarkenArrived");
    }
    return graph.finish();
}
}
