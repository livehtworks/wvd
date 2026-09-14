#include "dungeon_route.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/supply/dungeon_recover.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/recovery/revival.hpp"

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
    if (automatic_target(target))
        return C::any({C::image("NoChestCanBeFound"), C::image("theRouteToTheDestinationCannotBeFound")});
    if (target.position) {
        J reached{{"mode", target.target == "position" ? "reached" : "through_stair"}, {"position", *target.position}};
        if (target.target != "position")
            reached["image"] = target.target;
        return C::all({map, reached});
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
CompiledWorkflow traverse_dungeon(const WvdTaskPlan &plan, const J &profile,
                                 const std::set<std::string> &available_images, bool allow_download) {
    if (plan.route().empty() || plan.route().size() > 64)
        throw std::runtime_error("DUNGEON_ROUTE_SIZE_INVALID");
    // 组合段不能继承短子流程默认的 60 秒。400 秒取自旧无进展检测窗口，
    // 这里是有限段总上限，不冒充旧时序完全等价；更长任务需正常续段，不能扩大帧 TTL。
    C graph("tasks.dungeon_route." + plan.definition().id, std::chrono::seconds{400});
    const J combat{{"mode", "combat_active"}};
    const auto chest = C::any({C::image("chestFlag"), C::image("whowillopenit"), C::image("chestOpening")});
    const auto revive = C::image("RiseAgain");
    const auto encounter = C::any({combat, chest, revive});
    const auto map = C::all({C::image("mapFlag"), C::absent(encounter)});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(C::image("mapFlag")),
                                C::absent(encounter), C::absent(C::image("trait")), C::absent(C::image("recover"))});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("returntoTown"), C::image("returnText"),
                                        C::image("openworldmap"), C::image("worldmapflag")}),
                                 C::absent(C::image("mapFlag")), C::absent(encounter)});
    const auto inside = C::any({map, dungeon, encounter});
    graph.route("Entry", {"Outside", "Entered"});
    graph.confirm("Entered", "dungeon.enter", "dungeon_entered", inside, {"Dispatch"});
    graph.route("Dispatch", {"Blocked", "Combat", "Chest", "Revive", "Outside", "HealingPanel", "Resume", "Map"});
    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.observe("Blocked", {{"mode", "blocking_screen"}}, {"ClearBlocking"});
    graph.call_child("ClearBlocking", common, {"Dispatch"});
    graph.hit_limit("Blocked", 32);
    graph.hit_limit("ClearBlocking", 32);
    graph.hit_limit("Dispatch", 128);
    graph.observe("Outside", outside, {"Terminal"});
    const auto resurrection = graph.define_child("Resurrection", recovery::revive_after_defeat(), {"BlockedExit"});
    graph.observe("Revive", revive, {"Resurrect"});
    graph.hit_limit("Revive", 32);
    graph.call_child("Resurrect", resurrection, {"Dispatch"});
    graph.hit_limit("Resurrect", 32);

    const auto battle = graph.define_child("Battle", wvd::games::combat::fight_encounter(profile, available_images, 16), {"BlockedExit", "ReviveExit"});
    graph.observe("Combat", combat, {"Fight"});
    graph.call_child("Fight", battle, {"Dispatch"});
    graph.hit_limit("Combat", 128);
    graph.hit_limit("Fight", 128);
    const auto box = graph.define_child("Box", wvd::games::chest::open_chest(profile.at("WHO_WILL_OPEN_IT").get<int>(),
        profile.at("QUICK_DISARM_CHEST").get<bool>(), 0), {"CombatExit", "ReviveExit", "AmbushExit", "BlockedExit"});
    graph.observe("Chest", chest, {"OpenChest"});
    graph.call_child("OpenChest", box, {"Dispatch"});
    graph.hit_limit("Chest", 128);
    graph.hit_limit("OpenChest", 128);

    const auto heal = graph.define_child("Heal", supply::recover_in_dungeon(), {"EncounterExit", "BlockedExit"});
    graph.observe("HealingPanel", C::all({C::any({C::image("trait"), C::image("recover")}),
        C::absent(encounter), C::business("/healing_required", true)}), {"Heal"});
    graph.hit_limit("HealingPanel", 128);
    graph.confirm("Resume", "dungeon.resume", "dungeon_resumed", dungeon, {"Heal"});
    graph.hit_limit("Resume", 128);
    graph.call_child("Heal", heal, {"SelectPoint"});
    graph.hit_limit("Heal", 128);
    // 与旧 StateDungeon 一致，仅 Dungeon 分支调度角色恢复；已打开地图时不新增关闭地图动作。
    graph.observe("Map", map, {"SelectPoint"});
    graph.hit_limit("Map", 128);

    J points{"Blocked", "Combat", "Chest", "Revive", "Outside", "Finished"};
    for (std::size_t i = 0; i < plan.route().size(); ++i)
        points.push_back("Point" + std::to_string(i));
    graph.route("SelectPoint", points);
    graph.hit_limit("SelectPoint", 128);
    graph.observe("Finished", C::business("/task_step", plan.route().size()), {"Terminal"});
    for (std::size_t i = 0; i < plan.route().size(); ++i) {
        const auto &target = plan.route()[i];
        const auto suffix = std::to_string(i);
        const bool automatic = automatic_target(target);
        auto child = automatic ? navigation::auto_route(target.target)
                               : navigation::reach_map_target(target, plan.floor());
        J normal{{"EncounterExit", {"Dispatch"}}, {"BlockedExit", {"Dispatch"}}};
        if (automatic) {
            normal["StoppedExit"] = {"Dispatch"};
            if (target.target == "chest_auto")
                normal["UnavailableExit"] = {"Dispatch"};
        } else if (plan.floor())
            normal["FloorExit"] = {"Retreat"};
        const auto route = graph.append("Route" + suffix, child, {"Blocked", "Outside", "Confirm" + suffix}, normal);
        graph.observe("Point" + suffix, C::business("/task_step", i), {route});
        graph.hit_limit("Point" + suffix, 128);
        graph.confirm("Confirm" + suffix, "point." + suffix, "target_completed",
            C::all({C::absent(J{{"mode", "blocking_screen"}}), point_confirmation(target, map)}), {"Dispatch"}, i);
    }
    if (plan.floor()) {
        const auto retreat = graph.append("WrongFloor", navigation::auto_route("dungFlag"), {"Outside", "Dispatch"},
            {{"EncounterExit", {"Dispatch"}}, {"StoppedExit", {"Dispatch"}}, {"BlockedExit", {"Dispatch"}}});
        graph.route("Retreat", {retreat});
    }
    return graph.finish();
}
}
