#include "encounter.hpp"

namespace wvd::games::combat {
tasks::CompiledWorkflow fight_encounter(const nlohmann::json &profile,
                                         const std::set<std::string> &available_images,
                                         unsigned max_turns, unsigned max_auto_polls, EncounterEnd end) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (max_turns < 1 || max_turns > 16)
        throw std::runtime_error("COMBAT_TURN_BUDGET_INVALID");
    if (max_auto_polls < 1 || max_auto_polls > 256)
        throw std::runtime_error("COMBAT_AUTO_WAIT_BUDGET_INVALID");
    C graph("combat.encounter");
    const J battle{{"mode", "combat_active"}};
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(battle)});
    const auto chest = C::all({C::image("chestFlag"), C::absent(battle)});
    const bool repel = end == EncounterEnd::RepelPrompt;
    const auto special = profile.at("TASK_POINT_STRATEGY").value("special_combat", J::object());
    const bool detect_skull = !repel && special.value("skull", false);
    const bool detect_portrait = !repel && special.value("portrait", false);
    if (detect_skull || detect_portrait) {
        auto flee = C::image("flee");
        flee["roi"] = {720, 1120, 180, 130};
        const auto ready = C::all({battle, flee});
        J detectors = J::array();
        if (detect_skull) {
            auto skull = C::image("combat_special_skull");
            skull.update({{"roi", {160, 100, 740, 800}}, {"grayscale", true}, {"threshold", 0.84}});
            detectors.push_back(skull);
        }
        if (detect_portrait) {
            auto portrait = C::image(special.at("portrait_image").get<std::string>());
            portrait.update({{"roi", {20, 45, 160, 855}}, {"grayscale", true}, {"threshold", 0.88}});
            detectors.push_back(portrait);
        }
        const auto match = C::any(detectors);
        graph.route("Entry", {"Special", "Ordinary", "WaitingForMenu", "Dungeon", "Chest", "Revive"});
        graph.confirm("Special", "combat.begin", "combat_special_observed",
                      C::all({ready, match}), {"Turn0"});
        graph.confirm("Ordinary", "combat.begin", "combat_observed",
                      C::all({ready, C::absent(match)}), {"Turn0"});
        graph.observe("WaitingForMenu", C::all({battle, C::absent(flee)}), {"Entry"});
        graph.delay_after("WaitingForMenu", 500);
        graph.hit_limit("WaitingForMenu", 120);
    } else {
        graph.route("Entry", {"Observed", "Dungeon", "Chest", "Revive"});
        graph.confirm("Observed", "combat.begin", repel ? "repel_battle_observed" : "combat_observed", battle, {"Turn0"});
    }
    // 击退敌势力在战后对话结束一场战斗；不扩大普通遭遇的成功条件。
    graph.confirm("Dungeon", "combat.resume", repel ? "repel_battle_completed" : "dungeon_resumed",
        repel ? C::all({C::image("icanstillgo"), C::absent(battle)}) : dungeon, {"Terminal"});
    // 宝箱/复活不是 Dungeon resumed；计时和待计数遭遇留给外层返回地下城时结算。
    graph.observe("Chest", chest, repel ? J{"UnexpectedEnd"} : J{"Terminal"});
    if (repel) graph.recovery("UnexpectedEnd", "quest.repel_unexpected_encounter_end");
    graph.observe("Revive", C::image("RiseAgain"), {"ReviveExit"});
    graph.recovery("ReviveExit", "combat.revival_required");
    graph.recovery("BudgetExit", "combat.turn_budget_exhausted");
    graph.recovery("AutoTimeout", "combat.auto_progress_timeout");
    auto enabled = C::image("spellskill/CombatAutoEnable"), disabled = C::image("spellskill/CombatAutoDisable");
    enabled["roi"] = disabled["roi"] = {780, 1030, 120, 160};
    const auto popup = C::any({C::image("combat_skill_detail"), C::image("combat_skill_confirm"), C::image("close")});
    const auto clear = C::all({battle, C::absent(popup)});
    const auto full_auto = C::all({clear, enabled, C::business("/strategy/automatic", true)});
    const auto turn = graph.define_child("Actor", take_turn(profile, available_images), {"BlockedExit"});
    // 每次子调用独立计数；共享只读定义，不共享旧帧或动作许可。
    // 返回后先重新观察遭遇终点，再允许下一角色。根回合预算仍是显式有限链。
    for (unsigned index = 0; index < max_turns; ++index) {
        const auto name = "Turn" + std::to_string(index);
        const auto after = index + 1 < max_turns ? "Turn" + std::to_string(index + 1) : "BudgetExit";
        graph.call_child(name + "Action", turn, {"Dungeon", "Chest", "Revive", name + "Auto", after});
        graph.observe(name + "Auto", full_auto, {name + "Poll"});
        // Auto 就绪不是又一个角色行动。等待可暂时缺图，但不能由此发送新输入；
        // 只有明确退出/关闭 Auto 才继续，独立等待预算耗尽给出可追溯原因。
        graph.route(name + "Poll", {"Dungeon", "Chest", "Revive", name + "AutoOff", name + "Poll"});
        graph.delay_after(name + "Poll", 250);
        graph.hit_limit(name + "Poll", static_cast<int>(max_auto_polls));
        graph.failure_route(name + "Poll", {"AutoTimeout"});
        graph.observe(name + "AutoOff", C::all({clear, disabled, C::absent(enabled)}), {after});
        graph.route(name, {"Dungeon", "Chest", "Revive", name + "Action"});
    }
    graph.interrupt_on({{"mode", "blocking_screen"}}, "combat.common_screen_requires_dispatch");
    return graph.finish();
}
}
