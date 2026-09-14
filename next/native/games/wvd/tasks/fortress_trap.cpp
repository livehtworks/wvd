#include "fortress_trap.hpp"
#include "games/wvd/recovery/boot.hpp"

namespace wvd::games::tasks {
WvdTaskPlan fortress_trap_plan(const WvdQuestDefinition &definition) {
    if (definition.id != "fortress-B8F_trap" || definition.type != "quest")
        throw std::runtime_error("FORTRESS_TRAP_TASK_INVALID");
    // 固定旧 Farm case 的七个 TargetInfo；不是从同名 dungeon 猜出入本/回城路径。
    return WvdTaskPlan::parse(definition).with_route({
        {"stair_fortress1f", "左上", {720, 395}}, {"mark_auto"},
        {"position", "右下", {712, 972}}, {"position", "右下", {500, 1080}},
        {"position", "右下", {765, 918}}, {"position", "右下", {606, 1026}},
        {"stair_fortressGate", "左下", {720, 1027}}});
}
CompiledWorkflow fortress_trap_iteration(const WvdQuestDefinition &definition,
    const nlohmann::json &profile, const std::set<std::string> &images, bool allow_download) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    const auto plan = fortress_trap_plan(definition);
    const auto route = traverse_dungeon(plan, profile, images, allow_download);
    C graph("tasks.fortress_trap", route.time_limit + std::chrono::seconds{120});
    const auto inside = C::any({C::image("mapFlag"), C::image("dungFlag"), C::image("chestFlag"),
        C::image("whowillopenit"), C::image("RiseAgain"), J{{"mode", "combat_active"}}});
    const auto stable = C::all({inside, C::absent(J{{"mode", "blocking_screen"}})});
    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.route("Entry", {"Blocked", "Started"});
    graph.observe("Blocked", {{"mode", "blocking_screen"}}, {"ClearBlocking"});
    graph.call_child("ClearBlocking", common, {"Entry"});
    graph.hit_limit("Entry", 32);
    graph.hit_limit("Blocked", 32);
    graph.hit_limit("ClearBlocking", 32);
    graph.confirm("Started", "trap.start", "trap_cycle_started", stable, {"Traverse"});
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Traverse", dungeon, {"AllPoints", "PrematureExit"});
    graph.observe("AllPoints", C::business("/task_step", plan.route().size()), {"Completed"});
    graph.confirm("Completed", "trap.complete", "trap_cycle_completed", stable, {"Terminal"});
    graph.recovery("PrematureExit", "quest.trap_route_incomplete");
    return graph.finish();
}
}
