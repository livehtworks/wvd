#include "bull_cave.hpp"
#include "featured_request.hpp"
#include "games/wvd/quests/bull_cave.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/return_city.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include <algorithm>

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::BullCaveCycle::Phase;
const J positions{{"position", "左上", {134, 342}}, {"position", "右上", {500, 395}}, {"position", "右下", {340, 1027}}};
J phase(Phase p) { return C::all({C::business("/bull_cave/phase", static_cast<int>(p)), C::business("/bull_cave/unit_matches", true)}); }
}
WvdTaskPlan bull_cave_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "LBC-oneGorgon") throw std::runtime_error("BULL_CAVE_TASK_INVALID");
    auto points = positions;
    points.push_back({"LBC/LBC_quit", nullptr, nullptr});
    return WvdTaskPlan::parse(definition).with_route(points);
}
CompiledWorkflow bull_cave_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    const auto plan = bull_cave_plan(definition);
    const bool rest = profile.at("ACTIVE_REST").get<bool>();
    // 单个json值的大括号构造可能走复制构造；显式保留“路线包含一个目标”的数组层。
    const auto first_points = rest ? J::array({positions[0]}) : positions;
    const auto route = traverse_dungeon(plan.with_route(first_points), profile, images, allow_download);
    C graph("tasks.LBC-oneGorgon", std::max(route.time_limit + std::chrono::seconds{300}, std::chrono::milliseconds{900000}));
    const auto inn = C::image("Inn"), dung = C::image("dungFlag"), map = C::image("mapFlag");
    const auto royal_city = vision::royal_city();
    const auto leap_page = C::any({C::image("cursedWheelTitle"), C::image("cursedWheel"), C::image("ruins")});
    const auto outside = C::all({C::any({inn, C::image("EdgeOfTown"), C::image("returnText"), C::image("returntotown"), C::image("openworldmap")}), C::absent(map)});
    graph.route("Entry", {"Pending", "Active", "Start"});
    graph.observe("Pending", C::business("/bull_cave/leap_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.bull_cave_leap_unconfirmed");
    graph.observe("Active", C::business("/bull_cave/active", true), {"Stage"});
    graph.confirm("Start", "bull.start", rest ? "bull_cave_started_rest" : "bull_cave_started", leap_page, {"Stage"});
    graph.route("Stage", rest ? J{"LeapPhase", "FortressPhase", "RoyalPhase", "RequestPhase", "FirstEnterPhase", "FirstRoutePhase", "FirstExitPhase", "RestPhase", "SecondEnterPhase", "SecondRoutePhase", "SecondExitPhase"}
        : J{"LeapPhase", "FortressPhase", "RoyalPhase", "RequestPhase", "FirstEnterPhase", "FirstRoutePhase", "FirstExitPhase"});
    graph.observe("LeapPhase", phase(Phase::Leap), {"PrepareLeap"});
    graph.confirm("PrepareLeap", "bull.leap.prepare", "bull_cave_leap_prepared", leap_page, {"Leap"});
    const auto leap = graph.define_child("TimeLeap", profile.at("ACTIVE_CSC").get<bool>()
        ? navigation::time_leap_with_causality("GhostsOfYore", {"LBC/symbolofalliance", {{"LBC/EnaWasSaved", {2, 1, 0}}}}, "cursedwheel_impregnableFortress", allow_download)
        : navigation::time_leap_without_causality("GhostsOfYore", "cursedwheel_impregnableFortress", allow_download));
    graph.call_child("Leap", leap, {"Leaped"});
    graph.confirm("Leaped", "bull.leap", "bull_cave_leaped", outside, {"FortressPhase"});
    graph.delay_after("Leaped", 10000);
    graph.observe("FortressPhase", phase(Phase::Fortress), {"Fortress"});
    const auto fortress = graph.define_child("ReturnFortress", navigation::return_to_fortress());
    graph.call_child("Fortress", fortress, {"AtFortress"});
    graph.confirm("AtFortress", "bull.fortress", "bull_cave_fortress", inn, {"RoyalPhase"});
    graph.observe("RoyalPhase", phase(Phase::RoyalCity), {"Royal"});
    const auto royal = graph.define_child("RoyalCity", navigation::travel_city_to_city({"City_RoyalCityLuknalia", TaskSwipe{{450, 150}, {500, 150}}, {550, 1}}));
    graph.call_child("Royal", royal, {"AtRoyal"});
    graph.confirm("AtRoyal", "bull.royal", "bull_cave_royal", royal_city, {"RequestPhase"});
    graph.observe("RequestPhase", phase(Phase::Request), {"Request"});
    const auto request = graph.define_child("Featured", accept_featured_request(FeaturedRequest::BullCave, profile.at("ACTIVE_ROYALSUITE_REST").get<bool>()));
    graph.call_child("Request", request, {"Requested"});
    graph.confirm("Requested", "bull.request", "bull_cave_requested", inn, {"Terminal"});
    const auto enter = graph.define_child("EnterCave", navigation::travel_world({"LBC/LBC", TaskSwipe{{400, 400}, {400, 500}}, {550, 1}}, navigation::WorldArrival::DungeonEntrance));
    const auto exit = graph.define_child("LeaveCave", navigation::reach_map_target(plan.route().back()));
    for (const bool second : {false, true}) {
        if (second && !rest) continue;
        const std::string name = second ? "Second" : "First";
        graph.observe(name + "EnterPhase", phase(second ? Phase::EnterSecond : Phase::EnterFirst), {name + "Enter"});
        graph.call_child(name + "Enter", enter, {name + "Entered"});
        graph.confirm(name + "Entered", "bull.enter." + name, second ? "bull_cave_second_entered" : "bull_cave_first_entered", C::any({map, dung}), {name + "RoutePhase"});
        graph.observe(name + "RoutePhase", phase(second ? Phase::SecondRoute : Phase::FirstRoute), {name + "Route"});
        const auto child = graph.define_child(name + "Dungeon", second ? traverse_dungeon(plan.with_route(J::array({positions[1], positions[2]})), profile, images, allow_download) : route);
        graph.call_child(name + "Route", child, {name + "Points", "Incomplete"});
        graph.observe(name + "Points", C::business("/task_step", second ? 2 : rest ? 1 : 3), {name + "Routed"});
        graph.confirm(name + "Routed", "bull.route." + name, second ? "bull_cave_second_routed" : "bull_cave_first_routed", map, {name + "ExitPhase"});
        graph.observe(name + "ExitPhase", phase(second ? Phase::SecondExit : Phase::FirstExit), {name + "Exit"});
        graph.call_child(name + "Exit", exit, {name + "Exited"});
        graph.confirm(name + "Exited", "bull.exit." + name, second ? "bull_cave_completed" : "bull_cave_first_exited", outside,
            !second && rest ? J{"RestPhase"} : J{"Terminal"});
    }
    graph.recovery("Incomplete", "quest.bull_cave_route_incomplete");
    if (rest) {
        graph.observe("RestPhase", phase(Phase::Rest), {"Rest"});
        const auto sleep = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
        graph.call_child("Rest", sleep, {"Rested"});
        graph.confirm("Rested", "bull.rest", "bull_cave_rested", inn, {"Terminal"});
    }
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bull_cave_common_screen_requires_dispatch");
    return graph.finish();
}
}
