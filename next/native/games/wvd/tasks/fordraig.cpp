#include "fordraig.hpp"
#include "featured_request.hpp"
#include "games/wvd/quests/fordraig.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include <array>

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::FordraigCycle::Phase;
constexpr auto dialogue = recovery::DialoguePolicy::Fordraig;
constexpr const char *upper_left = "\u5de6\u4e0a";
constexpr const char *lower_left = "\u5de6\u4e0b";
J route_points(Phase value) {
    switch (value) {
    case Phase::Trap1Route: return {{"position", upper_left, {721, 448}}, {"position", upper_left, {720, 608}}};
    case Phase::Trap2Route: return {{"stair_down", upper_left, {721, 236}}, {"position", lower_left, {240, 921}}};
    case Phase::Trap3:
        // 固定旧源坐标为 (33,1238)，不能误拆成 (331,238)。
        return {{"position", lower_left, {33, 1238}}, {"stair_down", lower_left, {453, 1027}},
            {"position", lower_left, {187, 1027}}, {"stair_teleport", lower_left, {80, 1026}}};
    case Phase::PreBoss: return J::array({{"position", lower_left, {508, 1025}}});
    case Phase::Boss: return J::array({{"position", lower_left, {720, 1025}}});
    case Phase::Exit: return J::array({{"stair_teleport", upper_left, {665, 395}}});
    default: throw std::runtime_error("FORDRAIG_ROUTE_PHASE_INVALID");
    }
}
J phase(Phase value) {
    return C::all({C::business("/fordraig/phase", static_cast<int>(value)), C::business("/fordraig/unit_matches", true)});
}
J city() { return C::all({C::image("Inn"), C::absent(C::image("mapFlag")), C::absent(C::image("Stay"))}); }
J dungeon() {
    return C::all({C::image("dungFlag"), C::absent(C::image("mapFlag")),
        C::absent(J{{"mode", "combat_active"}}), C::absent(C::image("chestFlag"))});
}
CompiledWorkflow leap_to_fordraig() {
    C graph("quest.fordraig.leap", std::chrono::seconds{180});
    graph.use_dialogue(dialogue);
    const auto wheel = C::image("cursedWheel"), ruins = C::image("ruins"), special = C::image("specialRequest");
    const auto target = C::image("fordraig/Leap"), leap = C::image("leap"), ok = C::image("OK");
    const auto opening = C::any({city(), ruins, wheel, special, target});
    const auto selection = C::any({special, target, C::image("cursedWheelTitle")});
    graph.route("Entry", {"Wheel", "Ruins", "Dismiss"});
    graph.click("Wheel", wheel, wheel, selection, {"FindTarget"});
    graph.click("Ruins", C::all({ruins, C::absent(wheel)}), ruins, opening, {"Entry"});
    graph.fixed_click("Dismiss", C::all({city(), C::absent(wheel), C::absent(ruins)}), opening, {1, 1}, {"Entry"});
    graph.route("FindTarget", {"Select", "Special", "MenuDismiss"});
    graph.click("Select", target, target, C::any({leap, ok}), {"FindOK"});
    graph.click("Special", C::all({special, C::absent(target)}), special, selection, {"FindTarget"});
    graph.fixed_click("MenuDismiss", C::all({selection, C::absent(target), C::absent(special)}), selection, {1, 1}, {"FindTarget"});
    graph.route("FindOK", {"PrepareOK", "PrepareLeap"});
    graph.confirm("PrepareOK", "fordraig.leap.prepare", "fordraig_leap_prepared", ok, {"OK"});
    graph.confirm("PrepareLeap", "fordraig.leap.prepare", "fordraig_leap_prepared", C::all({leap, C::absent(ok)}), {"Leap"});
    graph.click("Leap", C::all({leap, C::absent(ok)}), leap, ok, {"OK"});
    graph.click("OK", ok, ok, city(), {"WaitRemaining"});
    graph.postcondition_budget("OK", 20000);
    graph.delay_after("OK", 10000);
    graph.route("WaitRemaining", {"Confirmed"});
    graph.delay_after("WaitRemaining", 5000);
    graph.confirm("Confirmed", "fordraig.leaped", "fordraig_leaped", city(), {"Terminal"});
    for (const auto *name : {"Entry", "Wheel", "Ruins", "Dismiss", "FindTarget", "Select", "Special", "MenuDismiss"}) {
        graph.hit_limit(name, 32);
        if (std::string{name} != "Entry" && std::string{name} != "FindTarget") graph.delay_after(name, 1000);
    }
    return graph.finish();
}
CompiledWorkflow push_trap(bool second) {
    C graph(second ? "quest.fordraig.trap2" : "quest.fordraig.trap1", std::chrono::seconds{180});
    graph.use_dialogue(dialogue);
    const auto map = C::image("mapFlag"), push = C::image("fordraig/TryPushingIt");
    const auto dung = dungeon(), search = C::all({dung, C::absent(push)});
    const auto known = C::any({dung, push});
    graph.route("Entry", {"Closed", "CloseMap"});
    graph.observe("Closed", dung, {"Find"});
    graph.back("CloseMap", map, dung, {"Find"});
    graph.route("Find", {"Prepare", "Turn"});
    const auto event = second ? "fordraig_trap2_prepared" : "fordraig_trap1_prepared";
    graph.confirm("Prepare", "fordraig.trap.prepare", event, push, {"Push"});
    graph.click("Push", push, push, C::all({dung, C::absent(push)}), {"Confirmed"});
    graph.postcondition_budget("Push", 10000);
    graph.confirm("Confirmed", "fordraig.trap.pushed", second ? "fordraig_trap2_completed" : "fordraig_trap1_completed",
        C::all({dung, C::absent(push)}), {"Terminal"});
    // 保留完整后备动作顺序；每次输入仍须使用新帧确认场景。
    graph.swipe("Turn", search, known, {100, 250, 800, 250}, {"Tap0"});
    for (int i = 0; i < 3; ++i) {
        const auto name = "Tap" + std::to_string(i);
        graph.fixed_click(name, known, known, {400, 800}, i == 2 ? J{"Find"} : J{"Tap" + std::to_string(i + 1)});
        graph.delay_after(name, i == 2 ? 1000 : 100); graph.hit_limit(name, 16);
    }
    graph.delay_after("CloseMap", 1000);
    graph.hit_limit("Find", 16); graph.hit_limit("Turn", 16);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fordraig_trap_common_screen_requires_dispatch");
    graph.stop_if_interrupted_after("Push", "quest.fordraig_trap_outcome_unconfirmed");
    return graph.finish();
}
CompiledWorkflow return_from_fordraig() {
    C graph("quest.fordraig.return", std::chrono::seconds{180});
    graph.use_dialogue(dialogue);
    const auto map = C::image("mapFlag"), dung = dungeon();
    const auto leave = C::image("leaveDung"), text = C::image("ReturnText"), royal = C::image("City_RoyalCityLuknalia");
    const auto leaving = C::any({dung, leave, text});
    const auto choice = C::any({royal, C::image("EdgeOfTown"), C::image("openworldmap"), city()});
    graph.route("Entry", {"AtCity", "ChooseCity", "Closed", "CloseMap"});
    graph.observe("AtCity", city(), {"Terminal"});
    graph.observe("ChooseCity", royal, {"Royal"});
    graph.observe("Closed", C::any({dung, leave, text}), {"FindReturn"});
    graph.back("CloseMap", map, leaving, {"FindReturn"});
    graph.route("FindReturn", {"ReturnText", "Leave", "Approach"});
    graph.click("ReturnText", text, text, choice, {"FindCity"});
    graph.click("Leave", C::all({leave, C::absent(text)}), leave, leaving, {"ReturnText", "Approach"});
    graph.fixed_click("Approach", C::all({C::any({dung, leave}), C::absent(text)}), leaving, {455, 1200}, {"FindReturn"});
    graph.route("FindCity", {"Royal", "AtCity", "CityBack"});
    graph.click("Royal", royal, royal, city(), {"AtCity"});
    graph.back("CityBack", C::all({choice, C::absent(royal), C::absent(city())}), choice, {"Royal", "AtCity", "CityDismiss"});
    graph.fixed_click("CityDismiss", C::all({choice, C::absent(royal), C::absent(city())}), choice, {1, 1}, {"FindCity"});
    for (const auto *name : {"ReturnText", "Leave", "Approach"}) {
        graph.delay_after(name, 3750); graph.hit_limit(name, 16);
    }
    for (const auto *name : {"CloseMap", "Royal", "CityBack", "CityDismiss"}) graph.delay_after(name, 1000);
    graph.hit_limit("FindReturn", 32); graph.hit_limit("FindCity", 32);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fordraig_return_common_screen_requires_dispatch");
    return graph.finish();
}
}
WvdTaskPlan fordraig_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "fordraig") throw std::runtime_error("FORDRAIG_TASK_INVALID");
    J points = J::array();
    for (const auto value : {Phase::Trap1Route, Phase::Trap2Route, Phase::Trap3, Phase::PreBoss, Phase::Boss, Phase::Exit})
        for (const auto &point : route_points(value)) points.push_back(point);
    return WvdTaskPlan::parse(definition)
        .with_entry({{"press", "intoWorldMap", {"fordraig/labyrinthOfFordraig", "input swipe 450 150 500 150"}, 1},
            {"press", "fordraig/Entrance", {"fordraig/labyrinthOfFordraig", {1, 1}}, 1}})
        .with_route(points);
}
std::vector<CompiledWorkflow> fordraig_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    const auto plan = fordraig_plan(definition);
    const std::array<Phase, 10> stages{Phase::Leap, Phase::Request, Phase::Enter, Phase::Trap1Route,
        Phase::Trap2Route, Phase::Trap3, Phase::PreBoss, Phase::Boss, Phase::Exit, Phase::Return};
    const std::array<std::string, 10> names{"Leap", "Request", "Enter", "Trap1", "Trap2", "Trap3", "PreBoss", "Boss", "Exit", "Return"};
    const std::array<std::string, 10> events{"fordraig_leaped", "fordraig_requested", "fordraig_entered",
        "fordraig_trap1_routed", "fordraig_trap2_routed", "fordraig_trap3_completed", "fordraig_preboss_completed",
        "fordraig_boss_completed", "fordraig_exited", "fordraig_completed"};
    std::vector<CompiledWorkflow> result;
    result.reserve(stages.size());
    for (std::size_t i = 0; i < stages.size(); ++i) {
        const auto stage = stages[i];
        const auto &name = names[i];
        const bool is_route = quests::FordraigCycle::route_points(stage) != 0;
        const bool mechanism = stage == Phase::Trap1Route || stage == Phase::Trap2Route;
        CompiledWorkflow work;
        if (is_route) work = traverse_dungeon(plan.with_route(route_points(stage)), profile, images, allow_download, dialogue);
        else if (stage == Phase::Leap) work = leap_to_fordraig();
        else if (stage == Phase::Request) work = accept_featured_request(FeaturedRequest::Fordraig, profile.at("ACTIVE_ROYALSUITE_REST").get<bool>());
        else if (stage == Phase::Enter) work = navigation::enter_dungeon(plan);
        else work = return_from_fordraig();
        const auto budget = work.time_limit + std::chrono::seconds{mechanism ? 300 : 120};
        if (budget > std::chrono::seconds{1800}) throw std::runtime_error("FORDRAIG_SEGMENT_BUDGET_EXCEEDED");
        C graph("tasks.fordraig." + name, budget);
        graph.use_dialogue(dialogue);
        const auto map = C::all({C::image("mapFlag"), C::absent(J{{"mode", "combat_active"}})});
        graph.route("Entry", stage == Phase::Leap ? J{"Pending", "Active", "StartCity", "StartRuins", "StartWheel"} : J{"Pending", "Active"});
        graph.observe("Pending", C::business("/fordraig/pending", true), {"Uncertain"});
        graph.recovery("Uncertain", "quest.fordraig_side_effect_unconfirmed");
        graph.observe("Active", C::business("/fordraig/active", true), {"Stage"});
        if (stage == Phase::Leap) {
            graph.confirm("StartCity", "fordraig.start", "fordraig_started", city(), {"Stage"});
            graph.confirm("StartRuins", "fordraig.start", "fordraig_started", C::image("ruins"), {"Stage"});
            graph.confirm("StartWheel", "fordraig.start", "fordraig_started", C::image("cursedWheel"), {"Stage"});
        }
        graph.route("Stage", mechanism ? J{"WorkPhase", "PushPhase"} : J{"WorkPhase"});
        graph.observe("WorkPhase", phase(stage), {"Work"});
        const auto child = graph.define_child(name + "Work", work);
        graph.call_child("Work", child, stage == Phase::Leap ? J{"Terminal"} : is_route ? J{"AllPoints", "Incomplete"} : J{"Confirmed"});
        if (is_route) {
            graph.observe("AllPoints", C::business("/task_step", quests::FordraigCycle::route_points(stage)), {"Confirmed"});
            graph.recovery("Incomplete", "quest.fordraig_route_incomplete");
        }
        if (stage != Phase::Leap) {
            const auto post = is_route ? map : stage == Phase::Enter ? C::any({map, dungeon()}) : city();
            graph.confirm("Confirmed", "fordraig.stage." + name, events[i], post, mechanism ? J{"PushPhase"} : J{"Terminal"});
        }
        if (mechanism) {
            graph.observe("PushPhase", phase(stage == Phase::Trap1Route ? Phase::Trap1Push : Phase::Trap2Push), {"Push"});
            const auto trap = graph.define_child(name + "Mechanism", push_trap(stage == Phase::Trap2Route));
            graph.call_child("Push", trap, {"Terminal"});
        }
        graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fordraig_common_screen_requires_dispatch");
        result.push_back(graph.finish());
    }
    return result;
}
}
