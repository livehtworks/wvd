#include "auto_combat.hpp"

namespace wvd::games::combat {
tasks::CompiledWorkflow single_actor_auto() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("combat.single_actor_auto");
    graph.check_policy("combat", {"wvd-network-retry", "wvd-pause", "wvd-download", "wvd-party-death", "wvd-party-defeat"});
    const auto image = [](const char *name, J roi) {
        auto value = C::image(name);
        value["roi"] = std::move(roi);
        return value;
    };
    const J battle{{"mode", "combat_active"}};
    const auto ended = C::all({C::any({C::image("dungFlag"), C::image("chestFlag"), C::image("RiseAgain")}), C::absent(battle)});
    const auto popup = C::any({C::image("combat_skill_detail"), C::image("combat_skill_confirm"),
                              image("close", {0, 600, 900, 1000})});
    const auto enabled = image("spellskill/CombatAutoEnable", {740, 940, 160, 280});
    const auto disabled = image("spellskill/CombatAutoDisable", {740, 940, 160, 280});
    const auto clear = C::absent(popup);
    const auto close = image("close", {0, 600, 900, 1000});
    const auto ok = C::image("combat_skill_confirm");
    const auto detail = C::image("combat_skill_detail");
    const auto clear_battle = C::all({battle, clear});
    const auto off = C::all({clear, disabled, C::absent(enabled)});
    const auto on = C::all({clear, enabled, C::absent(disabled)});
    const J choices{"Ended", "DisableAuto", "ClosePopup", "CancelPopup", "BackPopup", "Pulse"};
    graph.route("Entry", choices);
    graph.click("ClosePopup", C::all({battle, popup}), close, C::any({clear_battle, ended}), choices);
    graph.click("CancelPopup", C::all({battle, popup, C::absent(close)}), ok,
                C::any({clear_battle, ended}), choices, {-280, 0});
    graph.back("BackPopup", C::all({battle, detail, C::absent(close), C::absent(ok)}),
               C::any({clear_battle, ended}), choices);
    // User-selected 100 ms pulse, followed immediately by a fresh color-state observation.
    // This pair is never retried as a unit; any remaining ON state is only turned OFF.
    graph.click_pair("Pulse", C::all({clear_battle, disabled, C::absent(enabled)}), disabled,
        C::any({off, on, ended}), {"CheckOff"});
    graph.hit_limit("Pulse", 1);
    graph.route("CheckOff", {"Ended", "DisableAuto", "AlreadyDisabled"});
    graph.observe("Ended", ended, {"Terminal"});
    graph.click("DisableAuto", on, enabled, C::any({off, ended}), {"CheckOff"});
    graph.retry_menu_input("DisableAuto", on, 1000);
    graph.observe("AlreadyDisabled", off, {"Terminal"});
    // This settling delay is AFTER OFF confirmation; it must never defer the first color check.
    graph.delay_after("AlreadyDisabled", 2000);
    graph.interrupt_on(C::absent(J{{"mode", "input_clear"}}), "combat.common_screen_requires_dispatch");
    return graph.finish();
}

tasks::CompiledWorkflow enable_auto() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("combat.enable_auto");
    graph.check_policy("combat", {"wvd-network-retry", "wvd-pause", "wvd-download", "wvd-party-death", "wvd-party-defeat"});
    auto image = [](const std::string &name, J roi) {
        auto value = C::image(name);
        value["roi"] = std::move(roi);
        return value;
    };
    const J battle{{"mode", "combat_active"}};
    const auto ended = C::all({C::any({C::image("dungFlag"), C::image("chestFlag"), C::image("RiseAgain")}),
                               C::absent(battle)});
    const auto close = image("close", {0, 600, 900, 1000});
    const auto ok = C::image("combat_skill_confirm");
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
