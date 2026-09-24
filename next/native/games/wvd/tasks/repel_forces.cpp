#include "repel_forces.hpp"
#include "games/wvd/quests/repel_forces.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/supply/inn.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::RepelForces::Phase;
J phase(Phase value) {
    return C::all({C::business("/repel_forces/phase", static_cast<int>(value)), C::business("/repel_forces/unit_matches", true)});
}
CompiledWorkflow battle_pair(const J &profile, const std::set<std::string> &images) {
    C graph("quest.repel.pair", std::chrono::seconds{300});
    const auto prompt = C::image("icanstillgo"), withdraw = C::image("letswithdraw");
    const J combat{{"mode", "combat_active"}};
    const auto map = C::image("mapFlag"), dung = C::all({C::image("dungFlag"), C::absent(map), C::absent(combat)});
    const auto known = C::any({prompt, combat, dung});
    graph.route("Entry", {"BothDone", "FirstDone", "Map", "Ready", "Seek"});
    graph.observe("BothDone", C::business("/repel_forces/battles_in_pair", 2), {"Withdraw"});
    graph.observe("FirstDone", C::business("/repel_forces/battles_in_pair", 1), {"Prepare1"});
    graph.fixed_click("Map", map, known, {1, 1}, {"Ready", "Seek"});
    graph.observe("Ready", C::any({prompt, combat}), {"Prepare0"});
    // 保留原转向和点中心的顺序；仅已确认地下城允许，不在未知画面盲点。
    graph.swipe("Seek", C::all({dung, C::absent(prompt)}), known, {400, 400, 400, 100}, {"Ready", "Dismiss"});
    graph.fixed_click("Dismiss", C::all({dung, C::absent(prompt)}), known, {1, 1}, {"Ready", "Seek"});
    graph.hit_limit("Seek", 16); graph.hit_limit("Dismiss", 16);
    const auto encounter = graph.define_child("Encounter", wvd::games::combat::fight_encounter(profile, images, 16, 128, wvd::games::combat::EncounterEnd::RepelPrompt));
    for (int i = 0; i < 2; ++i) {
        const auto s = std::to_string(i);
        graph.confirm("Prepare" + s, "repel.prepare." + s, "repel_battle_prepared", C::any({prompt, combat}), {"AlreadyFighting" + s, "Continue" + s});
        graph.observe("AlreadyFighting" + s, combat, {"Fight" + s});
        graph.click("Continue" + s, C::all({prompt, C::absent(combat)}), prompt, combat, {"Fight" + s});
        graph.delay_after("Continue" + s, 1000);
        graph.call_child("Fight" + s, encounter, {i == 0 ? "Prepare1" : "Withdraw"});
    }
    graph.click("Withdraw", C::all({prompt, withdraw, C::absent(combat), C::business("/repel_forces/battles_in_pair", 2)}), withdraw, dung, {"Confirmed"});
    graph.delay_after("Withdraw", 1000);
    graph.confirm("Confirmed", "repel.pair", "repel_pair_completed", dung, {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}}, "quest.repel_common_screen_requires_dispatch");
    return graph.finish();
}
CompiledWorkflow leave_waterway() {
    C graph("quest.repel.return", std::chrono::seconds{180});
    const auto inn = C::image("Inn"), map = C::image("mapFlag"), leave = C::image("leaveDung"), back = C::image("returnText");
    const auto dung = C::image("dungFlag"), edge = C::image("EdgeOfTown");
    const auto known = C::any({inn, map, leave, back, dung, edge});
    graph.route("Entry", {"Done", "Return", "Leave", "Map", "Dismiss"});
    graph.observe("Done", C::all({inn, C::absent(map)}), {"Terminal"});
    graph.click("Return", C::all({back, C::absent(inn)}), back, known, {"Entry"});
    graph.click("Leave", C::all({leave, C::absent(inn), C::absent(back)}), leave, known, {"Entry"});
    graph.fixed_click("Map", C::all({map, C::absent(inn)}), known, {1, 1}, {"Entry"});
    graph.fixed_click("Dismiss", C::all({C::any({dung, edge}), C::absent(map), C::absent(inn), C::absent(back), C::absent(leave)}), known, {1, 1}, {"Entry"});
    for (const auto *node : {"Return", "Leave", "Map", "Dismiss"}) {
        graph.delay_after(node, 3000); graph.hit_limit(node, 16);
    }
    graph.hit_limit("Entry", 64);
    return graph.finish();
}
}
WvdTaskPlan repel_forces_plan(const WvdQuestDefinition &definition) {
    if (definition.id != "repelEnemyForces" || definition.type != "quest") throw std::runtime_error("REPEL_TASK_INVALID");
    return WvdTaskPlan::parse(definition)
        .with_entry({{"press", "TradeWaterway", "EdgeOfTown", 1}, {"press", "7thDist", {1, 1}, 1}})
        .with_route({{"position", "左下", {559, 599}}, {"position", "左下", {186, 813}}});
}
CompiledWorkflow repel_forces_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    quests::RepelForces::rounds(profile);
    const auto plan = repel_forces_plan(definition);
    const auto route = traverse_dungeon(plan, profile, images, allow_download);
    C graph("tasks.repelEnemyForces", route.time_limit + std::chrono::seconds{400});
    const auto inn = C::image("Inn"), map = C::image("mapFlag");
    graph.route("Entry", {"Pending", "Active", "Start"});
    graph.observe("Pending", C::business("/repel_forces/pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.repel_battle_outcome_unconfirmed");
    graph.observe("Active", C::business("/repel_forces/active", true), {"Stage"});
    graph.confirm("Start", "repel.start", "repel_started", inn, {"Stage"});
    graph.route("Stage", {"RestPhase", "RoutePhase", "PairPhase", "ExitPhase", "ReturnPhase"});
    graph.observe("RestPhase", phase(Phase::Rest), {"Rest"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", rest, {"Rested"});
    graph.confirm("Rested", "repel.rest", "repel_rested", inn, {"Enter"});
    const auto enter = graph.define_child("EntryMenu", navigation::enter_dungeon(plan));
    graph.call_child("Enter", enter, {"RoutePhase"});
    graph.observe("RoutePhase", phase(Phase::Route), {"Route"});
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Route", dungeon, {"Arrived"});
    graph.confirm("Arrived", "repel.arrive", "repel_arrived", map, {"Terminal"});
    graph.observe("PairPhase", phase(Phase::Pair), {"Pair"});
    const auto pair = graph.define_child("DoubleBattle", battle_pair(profile, images));
    graph.call_child("Pair", pair, {"Terminal"});
    graph.observe("ExitPhase", phase(Phase::Exit), {"Exit"});
    const auto exit = graph.define_child("ExitRoute", traverse_dungeon(plan.with_route({{"position", "左上", {612, 448}}}), profile, images, allow_download));
    graph.call_child("Exit", exit, {"Exited"});
    graph.confirm("Exited", "repel.exit", "repel_exited", map, {"ReturnPhase"});
    graph.observe("ReturnPhase", phase(Phase::Return), {"Return"});
    const auto returning = graph.define_child("ReturnCity", leave_waterway());
    graph.call_child("Return", returning, {"Completed"});
    graph.confirm("Completed", "repel.complete", "repel_completed", inn, {"Terminal"});
    return graph.finish();
}
}
