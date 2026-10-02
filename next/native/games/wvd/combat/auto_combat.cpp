#include "auto_combat.hpp"

namespace wvd::games::combat {
tasks::CompiledWorkflow enable_auto() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("combat.enable_auto");
    graph.check_policy("combat", {"wvd-network-retry", "wvd-pause", "wvd-download"});
    auto image = [](const std::string &name, J roi) {
        auto value = C::image(name);
        value["roi"] = std::move(roi);
        return value;
    };
    const J battle{{"mode", "combat_active"}};
    const auto ended = C::all({C::any({C::image("dungFlag"), C::image("chestFlag"), C::image("RiseAgain")}),
                               C::absent(battle)});
    const auto close = image("close", {120, 1330, 740, 270});
    const auto ok = image("combat_skill_confirm", {120, 1330, 740, 270});
    const auto detail = C::image("combat_skill_detail");
    const auto popup = C::any({detail, close, ok});
    const auto enabled = image("spellskill/CombatAutoEnable", {740, 940, 160, 280});
    const auto disabled = image("spellskill/CombatAutoDisable", {740, 940, 160, 280});
    const auto clear_battle = C::all({battle, C::absent(popup)});
    const auto done = C::all({clear_battle, enabled});
    const J choices{"BattleEnded", "Enabled", "ClosePopup", "CancelPopup", "BackPopup", "Enable", "Unknown0"};
    // 关闭后再出现详情，或动画期间战斗结束，都回到同一组有场景约束的候选。
    // 不进入只认识弹窗的无条件子路由，否则新帧已清场也无法退出。
    const J after_popup = choices;
    graph.route("Entry", choices);
    graph.observe("BattleEnded", ended, {"BattleEndedExit"});
    graph.recovery("BattleEndedExit", "combat.auto_not_confirmed_before_battle_end");
    graph.observe("Enabled", done, {"Terminal"});
    graph.click("ClosePopup", C::all({battle, popup}), close, C::any({clear_battle, ended}), after_popup);
    graph.retry_menu_input("ClosePopup", C::all({battle, popup, close}), 3000);
    graph.click("CancelPopup", C::all({battle, popup, C::absent(close)}), ok, C::any({clear_battle, ended}), after_popup,
                {-280, 0});
    graph.retry_menu_input("CancelPopup", C::all({battle, popup, C::absent(close), ok}), 3000);
    graph.back("BackPopup", C::all({battle, detail, C::absent(close), C::absent(ok)}), C::any({clear_battle, ended}),
               after_popup);
    for (auto name : {"ClosePopup", "CancelPopup", "BackPopup"})
        graph.hit_limit(name, 3);
    // 已知关闭才正常点击；新帧先查 Enabled，所以不会无条件双击把开关关回去。
    graph.fixed_click("Enable", C::all({clear_battle, disabled, C::absent(enabled)}), C::any({done, ended}),
                      {850, 1100}, {"BattleEnded", "Enabled", "Enable"});
    graph.retry_menu_input("Enable", C::all({clear_battle, disabled, C::absent(enabled)}), 3000);
    graph.failure_route("Enable", {"NoProgress"});
    graph.recovery("NoProgress", "combat.auto_progress_timeout");
    graph.hit_limit("Enable", 3);
    const auto unknown = C::all({clear_battle, C::absent(enabled), C::absent(disabled)});
    graph.observe("Unknown0", unknown, {"BattleEnded", "Enabled", "ClosePopup", "CancelPopup", "BackPopup", "Enable", "Unknown1"});
    graph.observe("Unknown1", unknown, {"BattleEnded", "Enabled", "ClosePopup", "CancelPopup", "BackPopup", "Enable", "Fallback"});
    graph.delay_after("Unknown0", 1000);
    graph.delay_after("Unknown1", 1000);
    // 保留旧三次未知后的保底点位，但点击后仍须正向确认，失败进入有界恢复出口。
    graph.fixed_click("Fallback", unknown, C::any({done, ended}), {850, 1100}, {"BattleEnded", "Enabled"});
    graph.hit_limit("Fallback", 1);
    graph.interrupt_on(C::absent(J{{"mode", "input_clear"}}), "combat.common_screen_requires_dispatch");
    return graph.finish();
}
} // namespace wvd::games::combat
