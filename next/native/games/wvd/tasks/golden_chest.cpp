#include "golden_chest.hpp"
#include "featured_request.hpp"
#include "games/wvd/quests/golden_chest.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/navigation/map_route.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::GoldenChestCycle::Phase;
const J points{{"position", "左上", {719, 1088}}, {"position", "左上", {346, 874}},
    {"chest", "左上", {{0, 0, 900, 1600}, {640, 0, 260, 1600}, {506, 0, 200, 700}}},
    {"chest", "右上", {{0, 0, 900, 1600}, {0, 0, 407, 1600}}},
    {"chest", "右下", {{0, 0, 900, 1600}, {0, 0, 900, 800}}},
    {"chest", "左下", {{0, 0, 900, 1600}, {650, 0, 250, 811}, {507, 166, 179, 165}}}};
J phase(Phase p) { return C::all({C::business("/golden_chest/phase", static_cast<int>(p)), C::business("/golden_chest/unit_matches", true)}); }
CompiledWorkflow golden_leap() {
    C graph("quest.golden_chest.leap", std::chrono::seconds{180});
    const auto ruins = C::image("ruins"), wheel = C::image("cursedWheel"), special = C::image("specialRequest");
    const auto target = C::image("SSC/Leap"), leap = C::image("leap"), ok = C::image("OK");
    const auto chooser = C::any({wheel, special, target, leap, ok});
    const auto outside = C::all({C::image("Inn"), C::absent(leap), C::absent(ok)});
    graph.route("Entry", {"Open", "Ruins"});
    graph.click("Ruins", ruins, ruins, chooser, {"Open"});
    graph.click("Open", wheel, wheel, chooser, {"Target", "Special"});
    graph.click("Special", C::all({special, C::absent(target)}), special, chooser, {"Target"});
    graph.click("Target", target, target, chooser, {"Confirm", "Leap"});
    graph.click("Leap", C::all({leap, C::absent(ok)}), leap, ok, {"Confirm"});
    graph.click("Confirm", ok, ok, outside, {"Done"});
    graph.delay_after("Confirm", 10000);
    graph.postcondition_budget("Confirm", 22000);
    graph.observe("Done", outside, {"Terminal"});
    return graph.finish();
}
CompiledWorkflow disable_trap() {
    C graph("quest.golden_chest.disable_trap", std::chrono::seconds{120});
    const auto trap = C::image("SSC/trapdeactived"), dung = C::image("dungFlag");
    const auto safe = C::all({dung, C::absent(C::image("mapFlag")), C::absent(J{{"mode", "combat_active"}}), C::absent(J{{"mode", "blocking_screen"}, {"parallel_basic", true}})});
    graph.route("Entry", {"DeactivateSeen", "Turn"});
    graph.observe("DeactivateSeen", trap, {"Dismiss"});
    graph.swipe("Turn", C::all({safe, C::absent(trap)}), C::any({safe, trap}), {450, 1050, 450, 850}, {"DeactivateSeen", "Interact"});
    graph.fixed_click("Interact", C::all({safe, C::absent(trap)}), C::any({safe, trap}), {445, 721}, {"Entry"});
    graph.delay_after("Interact", 4000);
    graph.fixed_click("Dismiss", trap, C::all({safe, C::absent(trap)}), {1, 1}, {"Terminal"});
    for (const auto *name : {"Entry", "Turn", "Interact"}) graph.hit_limit(name, 16);
    return graph.finish();
}
}
WvdTaskPlan golden_chest_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "SSC-goldenchest") throw std::runtime_error("GOLDEN_TASK_INVALID");
    auto route = points;
    route.push_back({"SSC/SSC_quit", "右下", nullptr});
    return WvdTaskPlan::parse(definition).with_route(route);
}
void configure_golden_chest_units(runtime::RunDefinition &definition, std::size_t cycles) {
    if (!cycles || cycles > 128 || definition.max_business_units != 1 || !definition.continuation_units.empty())
        throw std::runtime_error("GOLDEN_UNIT_BUDGET_INVALID");
    definition.max_business_units = cycles * 2;
    definition.continuation_units.assign(cycles * 2 - 1, definition.initial);
}
CompiledWorkflow golden_chest_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    const auto plan = golden_chest_plan(definition);
    const auto route = traverse_dungeon(plan.with_route(points), profile, images, allow_download, recovery::DialoguePolicy::GoldenChest);
    C graph("tasks.SSC-goldenchest", route.time_limit + std::chrono::seconds{600});
    const auto inn = C::image("Inn"), dung = C::image("dungFlag"), map = C::image("mapFlag");
    const auto leap_page = C::any({C::image("ruins"), C::image("cursedWheel")});
    graph.route("Entry", {"PendingLeap", "Active", "Start"});
    graph.observe("PendingLeap", C::business("/golden_chest/leap_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.golden_leap_unconfirmed");
    graph.observe("Active", C::business("/golden_chest/active", true), {"Stage"});
    graph.confirm("Start", "golden.start", "golden_started", leap_page, {"Stage"});
    graph.route("Stage", {"LeapPhase", "TravelPhase", "RequestPhase", "EnterPhase", "TrapPhase", "RoutePhase", "ExitPhase"});
    graph.observe("LeapPhase", phase(Phase::Leap), {"PrepareLeap"});
    graph.confirm("PrepareLeap", "golden.leap.prepare", "golden_leap_prepared", leap_page, {"Leap"});
    const auto leap = graph.define_child("SpecialLeap", golden_leap());
    graph.call_child("Leap", leap, {"Leaped"});
    graph.confirm("Leaped", "golden.leap.done", "golden_leaped", inn, {"TravelPhase"});
    graph.observe("TravelPhase", phase(Phase::Travel), {"Travel"});
    const auto travel = graph.define_child("RoyalCity", navigation::travel_city_to_city({"City_RoyalCityLuknalia", TaskSwipe{{450, 150}, {500, 150}}, {550, 1}}));
    graph.call_child("Travel", travel, {"Travelled"});
    graph.confirm("Travelled", "golden.travel", "golden_travelled", inn, {"RequestPhase"});
    graph.observe("RequestPhase", phase(Phase::Request), {"Request"});
    const auto request = graph.define_child("Featured", accept_featured_request(FeaturedRequest::GoldenChest, profile.at("ACTIVE_ROYALSUITE_REST").get<bool>()));
    graph.call_child("Request", request, {"Requested"});
    graph.confirm("Requested", "golden.request", "golden_requested", inn, {"Terminal"});
    graph.observe("EnterPhase", phase(Phase::Enter), {"Enter"});
    const auto enter = graph.define_child("EnterCave", navigation::travel_world({"SSC/SSC", TaskSwipe{{700, 500}, {600, 600}}, {550, 1}}, navigation::WorldArrival::DungeonEntrance));
    graph.call_child("Enter", enter, {"Entered"});
    graph.confirm("Entered", "golden.enter", "golden_entered", dung, {"TrapPhase"});
    graph.observe("TrapPhase", phase(Phase::Trap), {"Trap"});
    const auto trap = graph.define_child("DisableTrap", disable_trap());
    graph.call_child("Trap", trap, {"TrapDone"});
    graph.confirm("TrapDone", "golden.trap", "golden_trap_completed", dung, {"RoutePhase"});
    graph.observe("RoutePhase", phase(Phase::Route), {"Traverse"});
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Traverse", dungeon, {"AllPoints", "Incomplete"});
    graph.observe("AllPoints", C::business("/task_step", 6), {"RouteDone"});
    graph.confirm("RouteDone", "golden.route", "golden_route_completed", map, {"ExitPhase"});
    graph.recovery("Incomplete", "quest.golden_route_incomplete");
    graph.observe("ExitPhase", phase(Phase::Exit), {"Exit"});
    const auto exit = graph.define_child("LeaveCave", navigation::reach_map_target(plan.route().back()));
    graph.call_child("Exit", exit, {"Completed"});
    graph.confirm("Completed", "golden.done", "golden_completed", C::all({C::any({inn, C::image("openworldmap"), C::image("returnText"), C::image("EdgeOfTown")}), C::absent(map)}), {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.golden_common_screen_requires_dispatch");
    return graph.finish();
}
}
