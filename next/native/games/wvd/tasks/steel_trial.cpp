#include "steel_trial.hpp"
#include "games/wvd/quests/steel_trial.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/recovery/boot.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::SteelTrial::Phase;
J phase(Phase p) { return C::all({C::business("/steel_trial/phase", static_cast<int>(p)), C::business("/steel_trial/unit_matches", true)}); }
CompiledWorkflow request_trial() {
    C graph("quest.steeltrail.request", std::chrono::seconds{180});
    const auto guild = C::image("guild"), request = C::image("guildRequest"), exam = C::image("gradeexam"), steel = C::image("Steel");
    const auto menus = C::any({guild, request, exam, steel});
    graph.route("Entry", {"Ready", "Guild", "Request", "Exam", "Scroll"});
    graph.observe("Ready", steel, {"Prepare"});
    graph.click("Guild", C::all({guild, C::absent(request), C::absent(exam), C::absent(steel)}), guild, menus, {"Entry"});
    graph.click("Request", C::all({request, C::absent(exam), C::absent(steel)}), request, menus, {"Ready", "Exam", "Scroll"});
    graph.click("Exam", C::all({exam, C::absent(steel)}), exam, menus, {"Entry"});
    graph.swipe("Scroll", C::all({request, C::absent(exam), C::absent(steel)}), menus, {600, 1400, 300, 1400}, {"Entry"});
    graph.confirm("Prepare", "steel.select.prepare", "steel_trial_prepared", steel, {"Select"});
    const auto arrived = C::any({C::image("mapFlag"), C::image("dungFlag"), C::image("ready")});
    graph.click("Select", steel, steel, arrived, {"Terminal"}, {306, 258});
    for (const auto *name : {"Entry", "Guild", "Request", "Exam", "Scroll"}) graph.hit_limit(name, 32);
    return graph.finish();
}
CompiledWorkflow return_from_trial(const J &profile, const std::set<std::string> &images, bool download) {
    C graph("quest.steeltrail.return", std::chrono::seconds{240});
    const auto city = C::all({C::image("Inn"), C::absent(C::image("mapFlag"))});
    const auto map = C::image("mapFlag"), combat = J{{"mode", "combat_active"}};
    const auto dialogue = J{{"mode", "blocking_screen"}};
    const auto known = C::any({city, map, C::image("dungFlag"), combat, dialogue});
    graph.route("Entry", {"City", "Dialogue", "Combat", "CloseMap", "Wait"});
    graph.observe("City", city, {"Terminal"});
    graph.observe("Dialogue", dialogue, {"Clear"});
    const auto clear = graph.define_child("Common", recovery::clear_common_screens(download, recovery::DialoguePolicy::SteelTrial));
    graph.call_child("Clear", clear, {"Entry"});
    graph.observe("Combat", combat, {"Fight"});
    const auto fight = graph.define_child("Encounter", wvd::games::combat::fight_encounter(profile, images, 16));
    graph.call_child("Fight", fight, {"Entry"});
    // 旧后续菜单/住宿助手会点1,1关闭地图。只在新帧证实地图时沿用，未知页不盲点。
    graph.fixed_click("CloseMap", C::all({map, C::absent(dialogue), C::absent(combat)}), known, {1, 1}, {"Entry"});
    graph.observe("Wait", C::all({C::image("dungFlag"), C::absent(map)}), {"Entry"});
    graph.delay_after("Wait", 1000);
    for (const auto *name : {"Entry", "Dialogue", "Clear", "Combat", "Fight", "CloseMap", "Wait"}) graph.hit_limit(name, 32);
    graph.use_dialogue(recovery::DialoguePolicy::SteelTrial);
    return graph.finish();
}
}
WvdTaskPlan steel_trial_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "steeltrail") throw std::runtime_error("STEEL_TRIAL_TASK_INVALID");
    return WvdTaskPlan::parse(definition).with_route({{"position", "左上", {131, 769}}, {"position", "左上", {827, 447}},
        {"position", "左上", {131, 769}}, {"position", "左下", {719, 1080}}});
}
CompiledWorkflow steel_trial_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    if (profile.at("REST_INTERVEL").get<std::int64_t>() < 0) throw std::runtime_error("STEEL_TRIAL_REST_INTERVAL_INVALID");
    const auto route = traverse_dungeon(steel_trial_plan(definition), profile, images, allow_download, recovery::DialoguePolicy::SteelTrial);
    C graph("tasks.steeltrail", route.time_limit + std::chrono::seconds{480});
    graph.use_dialogue(recovery::DialoguePolicy::SteelTrial);
    const auto city = C::image("Inn"), map = C::image("mapFlag");
    const auto menus = C::any({city, C::image("guild"), C::image("guildRequest"), C::image("gradeexam"), C::image("Steel")});
    graph.route("Entry", {"Pending", "Active", "Start"});
    graph.observe("Pending", C::business("/steel_trial/pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.steel_trial_selection_unconfirmed");
    graph.observe("Active", C::business("/steel_trial/active", true), {"Stage"});
    graph.confirm("Start", "steel.start", "steel_trial_started", menus, {"Stage"});
    graph.route("Stage", {"RequestPhase", "RoutePhase", "ReturnPhase", "RestPhase"});
    graph.observe("RequestPhase", phase(Phase::Request), {"Request"});
    const auto request = graph.define_child("RequestMenu", request_trial());
    graph.call_child("Request", request, {"Entered"});
    graph.confirm("Entered", "steel.enter", "steel_trial_entered", C::any({map, C::image("dungFlag"), C::image("ready")}), {"Arrival"});
    const auto arrival = graph.define_child("ArrivalDialogue", recovery::clear_common_screens(allow_download, recovery::DialoguePolicy::SteelTrial));
    graph.call_child("Arrival", arrival, {"RoutePhase"});
    graph.observe("RoutePhase", phase(Phase::Route), {"Route"});
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Route", dungeon, {"AllPoints", "Incomplete"});
    graph.observe("AllPoints", C::business("/task_step", 4), {"Routed"});
    graph.recovery("Incomplete", "quest.steel_trial_route_incomplete");
    graph.confirm("Routed", "steel.route", "steel_trial_routed", map, {"ReturnPhase"});
    graph.observe("ReturnPhase", phase(Phase::Return), {"Return"});
    const auto returning = graph.define_child("ReturnCity", return_from_trial(profile, images, allow_download));
    graph.call_child("Return", returning, {"Returned"});
    graph.confirm("Returned", "steel.return", "steel_trial_returned", city, {"RestPhase"});
    graph.observe("RestPhase", phase(Phase::Rest), {"RestDue", "NoRest"});
    graph.observe("RestDue", C::business("/steel_trial/rest_due", true), {"Rest"});
    graph.observe("NoRest", C::business("/steel_trial/rest_due", false), {"Completed"});
    const auto inn = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", inn, {"Completed"});
    graph.confirm("Completed", "steel.complete", "steel_trial_completed", city, {"Terminal"});
    return graph.finish();
}
}
