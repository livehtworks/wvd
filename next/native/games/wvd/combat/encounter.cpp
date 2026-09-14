#include "encounter.hpp"

namespace wvd::games::combat {
tasks::CompiledWorkflow fight_encounter(const nlohmann::json &profile,
                                         const std::set<std::string> &available_images,
                                         unsigned max_turns) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (max_turns < 1 || max_turns > 16)
        throw std::runtime_error("COMBAT_TURN_BUDGET_INVALID");
    C graph("combat.encounter");
    const J battle{{"mode", "combat_active"}};
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(battle)});
    const auto chest = C::all({C::image("chestFlag"), C::absent(battle)});
    graph.route("Entry", {"Observed", "Dungeon", "Chest", "Revive"});
    graph.confirm("Observed", "combat.begin", "combat_observed", battle, {"Turn0"});
    graph.confirm("Dungeon", "combat.resume", "dungeon_resumed", dungeon, {"Terminal"});
    // 宝箱/复活不是 Dungeon resumed；计时和待计数遭遇留给外层返回地下城时结算。
    graph.observe("Chest", chest, {"Terminal"});
    graph.observe("Revive", C::image("RiseAgain"), {"ReviveExit"});
    graph.recovery("ReviveExit", "combat.revival_required");
    graph.recovery("BudgetExit", "combat.turn_budget_exhausted");
    const auto turn = graph.define_child("Actor", take_turn(profile, available_images));
    // 每次真实 Maa 子任务拥有独立的节点预算；共享的是只读图而不是旧帧/动作许可。
    // 返回后先重新观察遭遇终点，再允许下一角色。根回合预算仍是显式有限链。
    for (unsigned index = 0; index < max_turns; ++index) {
        const auto name = "Turn" + std::to_string(index);
        const auto after = index + 1 < max_turns ? "Turn" + std::to_string(index + 1) : "BudgetExit";
        graph.call_child(name + "Action", turn, {"Dungeon", "Chest", "Revive", after});
        graph.route(name, {"Dungeon", "Chest", "Revive", name + "Action"});
    }
    return graph.finish();
}
}
