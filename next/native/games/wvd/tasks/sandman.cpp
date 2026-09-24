#include "sandman.hpp"
#include "games/wvd/quests/sandman.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/supply/inn.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::SandmanCycle::Phase;
const J positions{{"position", "左下", {133, 814}}, {"position", "左下", {238, 1076}}, {"position", "左下", {450, 924}}};
J phase(Phase p) { return C::all({C::business("/sandman/phase", static_cast<int>(p)), C::business("/sandman/unit_matches", true)}); }
}
WvdTaskPlan sandman_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "sandman") throw std::runtime_error("SANDMAN_TASK_INVALID");
    auto points = positions;
    points.push_back({"harken2", "左下", nullptr});
    return WvdTaskPlan::parse(definition).with_floor("stair_fortress3f")
        .with_entry({{"press", "impregnableFortress", {"EdgeOfTown", {1, 1}}, 1},
                     {"press", "fortressb3f", "input swipe 650 250 650 900", 1}}).with_route(points);
}
CompiledWorkflow sandman_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    const auto plan = sandman_plan(definition);
    // 原case的局部覆盖仅影响此编译产物，不写回冻结profile或其它任务。
    auto local = profile;
    local["RE_ASSEMBLE_PARTY"] = false;
    local["BYPASS_THE_WALL"] = false;
    const auto route = traverse_dungeon(plan.with_route(positions), local, images, allow_download, recovery::DialoguePolicy::Sandman);
    C graph("tasks.sandman", route.time_limit + std::chrono::seconds{180});
    graph.use_dialogue(recovery::DialoguePolicy::Sandman);
    const auto map = C::image("mapFlag"), inn = C::image("Inn"), dung = C::image("dungFlag");
    const auto city = C::all({inn, C::absent(map), C::absent(C::image("Stay"))});
    graph.route("Entry", {"Pending", "CompletedVisit", "Active", "Start", "StartDungeon", "StartEdge", "StartFortress", "StartFloor"});
    graph.observe("CompletedVisit", C::business("/sandman/completed_unit_matches", true), {"Terminal"});
    graph.observe("Pending", C::business("/sandman/leap_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.sandman_leap_unconfirmed");
    graph.observe("Active", C::business("/sandman/active", true), {"Stage"});
    // 五种入口分别确认，避免一次确认扫描所有互斥页面而耗尽帧有效期。
    graph.confirm("Start", "sandman.start", "sandman_started", map, {"Stage"});
    graph.confirm("StartDungeon", "sandman.start", "sandman_started", dung, {"Stage"});
    graph.confirm("StartEdge", "sandman.start", "sandman_started", C::image("EdgeOfTown"), {"Stage"});
    graph.confirm("StartFortress", "sandman.start", "sandman_started", C::image("impregnableFortress"), {"Stage"});
    graph.confirm("StartFloor", "sandman.start", "sandman_started", C::image("fortressb3f"), {"Stage"});
    graph.route("Stage", {"EnterPhase", "RoutePhase", "ExitPhase", "DecidePhase", "RestDukePhase", "LeapDukePhase", "RestTriumphPhase", "LeapTriumphPhase"});
    graph.observe("EnterPhase", phase(Phase::Enter), {"Enter"});
    const auto entry = graph.define_child("DungeonEntry", navigation::enter_dungeon(plan));
    graph.call_child("Enter", entry, {"Entered"});
    graph.confirm("Entered", "sandman.enter", "sandman_entered", C::any({dung, map}), {"RoutePhase"});
    graph.observe("RoutePhase", phase(Phase::Route), {"Route"});
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Route", dungeon, {"AllPoints", "Incomplete"});
    graph.observe("AllPoints", C::business("/task_step", 3), {"Routed"});
    graph.confirm("Routed", "sandman.route", "sandman_routed", map, {"ExitPhase"});
    graph.recovery("Incomplete", "quest.sandman_route_incomplete");
    graph.observe("ExitPhase", phase(Phase::Exit), {"Exit"});
    const auto exit = graph.define_child("HarkenExit", navigation::reach_map_target(plan.route().back()));
    graph.call_child("Exit", exit, {"Exited"});
    const auto outside = C::all({C::any({inn, C::image("EdgeOfTown"), C::image("returnText"), C::image("openworldmap")}), C::absent(map)});
    graph.confirm("Exited", "sandman.exit", "sandman_exited", outside, {"DecidePhase"});
    graph.observe("DecidePhase", phase(Phase::Decide), {"Decided"});
    // 寻缘和跳跃分开有限正常段；无缘的第二段只核对访问回执，不发出住宿/跳跃输入。
    graph.confirm("Decided", "sandman.decide", "sandman_decided", outside, {"Terminal"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(local.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    for (const bool triumph : {false, true}) {
        const std::string name = triumph ? "Triumph" : "Duke";
        graph.observe("Rest" + name + "Phase", phase(triumph ? Phase::RestTriumph : Phase::RestDuke), {"Rest" + name});
        graph.call_child("Rest" + name, rest, {name + "Rested"});
        graph.confirm(name + "Rested", "sandman.rest." + name, triumph ? "sandman_triumph_rested" : "sandman_duke_rested", city, {"Leap" + name + "Phase"});
        graph.observe("Leap" + name + "Phase", phase(triumph ? Phase::LeapTriumph : Phase::LeapDuke), {"Prepare" + name});
        graph.confirm("Prepare" + name, "sandman.prepare." + name, triumph ? "sandman_triumph_prepared" : "sandman_duke_prepared", city, {"Leap" + name});
        const auto leap = graph.define_child("TimeLeap" + name, navigation::time_leap_without_causality(
            triumph ? "Triumph" : "requestToRescueTheDuke", "cursedwheel_impregnableFortress", allow_download));
        graph.call_child("Leap" + name, leap, {name + "Leaped"});
        graph.confirm(name + "Leaped", "sandman.leap." + name, triumph ? "sandman_completed" : "sandman_duke_leaped",
            triumph ? outside : city, triumph ? J{"Terminal"} : J{"RestTriumphPhase"});
        if (!triumph) graph.delay_after(name + "Leaped", 10000);
    }
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.sandman_common_screen_requires_dispatch");
    return graph.finish();
}
}
