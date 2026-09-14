#include "auto_combat.hpp"

namespace wvd::games::combat {
tasks::CompiledWorkflow enable_auto() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("combat.enable_auto");
    auto image = [](const std::string &name, J roi) {
        auto value = C::image(name);
        value["roi"] = std::move(roi);
        return value;
    };
    J active = J::array();
    for (auto name : {"combatActive", "combatActive_2", "combatActive_3", "combatActive_4"})
        active.push_back(image(name, {0, 0, 150, 80}));
    const auto battle = C::any(active);
    const auto close = image("close", {250, 1420, 420, 150});
    const auto ok = image("OK", {420, 1420, 340, 160});
    const auto detail = C::image("spellskill/skillDetail");
    const auto popup = C::any({detail, close, ok});
    const auto enabled = image("spellskill/CombatAutoEnable", {780, 1030, 120, 160});
    const auto disabled = image("spellskill/CombatAutoDisable", {780, 1030, 120, 160});
    const auto clear_battle = C::all({battle, C::absent(popup)});
    const auto done = C::all({clear_battle, enabled});
    const J choices{"Enabled", "ClosePopup", "CancelPopup", "BackPopup", "Enable", "Unknown0"};
    const J after_popup{"Enabled", "Enable", "Unknown0", "RetryPopup"};
    graph.route("Entry", choices);
    graph.route("RetryPopup", {"ClosePopup", "CancelPopup", "BackPopup"});
    graph.hit_limit("RetryPopup", 2);
    graph.observe("Enabled", done, {"Terminal"});
    graph.click("ClosePopup", C::all({battle, popup}), close, battle, after_popup);
    graph.click("CancelPopup", C::all({battle, popup, C::absent(close)}), ok, battle, after_popup,
                {-280, 0});
    graph.back("BackPopup", C::all({battle, detail, C::absent(close), C::absent(ok)}), battle,
               after_popup);
    for (auto name : {"ClosePopup", "CancelPopup", "BackPopup"})
        graph.hit_limit(name, 3);
    // 已知关闭才正常点击；新帧先查 Enabled，所以不会无条件双击把开关关回去。
    graph.fixed_click("Enable", C::all({clear_battle, disabled, C::absent(enabled)}), battle,
                      {850, 1100}, {"Enabled", "Enable"});
    graph.hit_limit("Enable", 3);
    const auto unknown = C::all({clear_battle, C::absent(enabled), C::absent(disabled)});
    graph.observe("Unknown0", unknown, {"Enabled", "Enable", "Unknown1"});
    graph.observe("Unknown1", unknown, {"Enabled", "Enable", "Fallback"});
    // 保留旧三次未知后的保底点位，但点击后仍须正向确认，失败进入有界恢复出口。
    graph.fixed_click("Fallback", unknown, battle, {850, 1100}, {"Enabled"});
    graph.hit_limit("Fallback", 1);
    return graph.finish();
}
} // namespace wvd::games::combat
