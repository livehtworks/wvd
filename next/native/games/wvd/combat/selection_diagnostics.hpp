#pragma once
#include "strategy.hpp"
#include <cmath>

namespace wvd::games::combat {
// Recognition and action availability are independent: consumed rows must not erase identity.
inline nlohmann::json selection_diagnostics(const nlohmann::json &summary,
                                           const std::vector<PortraitScore> &scores) {
    using J = nlohmann::json;
    const auto &strategy = summary.at("strategy");
    const auto &group = strategy.at("current");
    const bool selected = summary.at("has_prepared_skill");
    const PortraitScore *best = nullptr;
    for (const auto &score : scores)
        if (std::isfinite(score.score) && (!best || score.score > best->score)) best = &score;
    const bool recognized = best && best->score >= .80;
    std::size_t remaining{};
    for (const auto &row : group.value("skill_settings", J::array())) {
        const auto role = row.value("role_var", "");
        if (recognized && !role.empty() &&
            (best->portrait == role || best->portrait == role + "_sp" || best->portrait == role + "_alt"))
            ++remaining;
    }
    const auto name = group.value("group_name", "");
    std::string reason;
    if (selected) reason = "selected";
    else if (group.empty()) reason = "strategy_unavailable";
    else if (name == "全自动战斗" || name == "Full Auto") reason = "explicit_full_auto";
    else if (group.value("skill_settings", J::array()).empty()) reason = "strategy_actions_exhausted";
    else if (!recognized) reason = "portrait_unrecognized";
    else if (!remaining) reason = "recognized_actor_no_remaining_action";
    else reason = "recognized_actor_action_not_selected";
    return {{"has_skill", selected}, {"portrait", selected ? summary.at("prepared_portrait") :
                J(recognized ? best->portrait : "")},
        {"recognized_portrait", recognized ? best->portrait : ""}, {"portrait_recognized", recognized},
        {"best_portrait", best ? best->portrait : ""}, {"best_score", best ? J(best->score) : J(nullptr)},
        {"remaining_actor_actions", remaining}, {"skill_index", summary.at("prepared_skill_index")},
        {"strategy_epoch", strategy.at("epoch")}, {"strategy_name", name},
        {"reason", reason}, {"threshold", .80}};
}
} // namespace wvd::games::combat
