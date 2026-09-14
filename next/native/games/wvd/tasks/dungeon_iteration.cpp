#include "dungeon_iteration.hpp"
#include "departure.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/recovery/boot.hpp"

namespace wvd::games::tasks {
CompiledWorkflow dungeon_iteration(const WvdTaskPlan &plan, const nlohmann::json &profile,
                                    const std::set<std::string> &available_images, bool allow_download) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    if (plan.definition().type != "dungeon")
        throw std::runtime_error("DUNGEON_ITERATION_TYPE_INVALID");
    C graph("tasks.dungeon_iteration." + plan.definition().id, std::chrono::seconds{760});
    const auto inside = C::any({C::image("dungFlag"), C::image("mapFlag"), C::image("chestFlag"),
                                C::image("whowillopenit"), C::image("RiseAgain"), J{{"mode", "combat_active"}}});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("returntoTown"),
                                        C::image("openworldmap"), C::image("returnText"), C::image("worldmapflag")}),
                                 C::absent(inside)});
    const auto entrance = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("returntoTown"), C::image("openworldmap")}),
                                  C::absent(inside), C::absent(C::image("worldmapflag")), C::absent(C::image("returnText"))});
    graph.route("Entry", {"Blocked", "AlreadyInside", "Outside"});
    graph.hit_limit("Entry", 32);
    graph.observe("Blocked", {{"mode", "blocking_screen"}}, {"ClearBlocking"});
    graph.hit_limit("Blocked", 32);
    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.call_child("ClearBlocking", common, {"Entry"});
    graph.hit_limit("ClearBlocking", 32);
    graph.observe("AlreadyInside", inside, {"Traverse"});
    graph.observe("Outside", outside, {"Prepare"});
    const auto prepare = graph.append("Departure", prepare_departure(plan, profile), {"CountDeparture"},
        {{"InsideExit", {"AlreadyInside"}}});
    graph.route("Prepare", {prepare});
    // 与旧 DungeonFarm 一致，在 EOT 前结算上一轮；从本内启动不伪造一次 EOT 结算。
    graph.confirm("CountDeparture", "farm.departure", "dungeon_completed", entrance, {"EnterDungeon"});
    const auto enter = graph.append("Enter", navigation::enter_dungeon(plan), {"Traverse"});
    graph.route("EnterDungeon", {enter});
    const auto route = graph.define_child("Dungeon", traverse_dungeon(plan, profile, available_images, allow_download));
    graph.call_child("Traverse", route, {"Terminal"});
    return graph.finish();
}
}
