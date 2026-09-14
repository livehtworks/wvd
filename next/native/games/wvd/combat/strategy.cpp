#include "strategy.hpp"
#include "games/wvd/profile.hpp"
#include <cmath>

namespace wvd::games {
using J = nlohmann::json;
CombatStrategy::CombatStrategy(J profile)
    : profile_(std::move(profile)), english_(profile_.at("LANGUAGE") == "en_US") {
    validate_strategy(profile_.at("STRATEGY"));
    if (!profile_.at("TASK_SPECIFIC_CONFIG").is_boolean() ||
        !profile_.at("DEFAULT_OVERALL_STRATEGY").is_string() ||
        !profile_.at("TASK_POINT_STRATEGY").is_object())
        throw std::runtime_error("STRATEGY_PROFILE_INVALID");
}
bool CombatStrategy::uses_task_points() const {
    return profile_.at("TASK_SPECIFIC_CONFIG").get<bool>() &&
           profile_.at("TASK_POINT_STRATEGY").value("overall_strategy", "") ==
               (english_ ? "Custom Task Point Strategy" : "自定义任务点策略");
}
void CombatStrategy::reload(std::size_t task_step) {
    std::string key;
    if (!profile_.at("TASK_SPECIFIC_CONFIG").get<bool>())
        key = profile_.at("DEFAULT_OVERALL_STRATEGY").get<std::string>();
    else if (uses_task_points())
        key = profile_.at("TASK_POINT_STRATEGY")
                  .value("task_point", J::object())
                  .value(std::to_string(task_step), "");
    else
        key = profile_.at("TASK_POINT_STRATEGY").value("overall_strategy", "");
    current_ = J::object();
    // 固定旧源选择首个同名分组；深复制后消费，不修改冻结配置。
    for (const auto &group : profile_.at("STRATEGY")) {
        if (group.value("group_name", "") == key) {
            current_ = group;
            break;
        }
    }
    ++epoch_;
}
bool CombatStrategy::automatic() const {
    return current_.empty() ||
           current_.value("group_name", "") == (english_ ? "Full Auto" : "全自动战斗") ||
           current_.value("skill_settings", J::array()).empty();
}
std::optional<SkillSelection>
CombatStrategy::select(const std::vector<PortraitScore> &scores) const {
    if (automatic())
        return std::nullopt;
    double highest = 0;
    std::optional<SkillSelection> selected;
    const auto &rows = current_.at("skill_settings");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto role = rows[i].value("role_var", "");
        if (role.empty())
            continue;
        for (const auto &candidate : {role, role + "_sp", role + "_alt"})
            for (const auto &score : scores) {
                // 相关系数允许负数；负相关是未匹配，不是视觉后端错误。
                if (!std::isfinite(score.score) || score.score < -1 || score.score > 1)
                    throw std::runtime_error("PORTRAIT_SCORE_INVALID");
                if (score.portrait == candidate && score.score > highest) {
                    highest = score.score;
                    selected = SkillSelection{"", 0, epoch_, i, rows[i]};
                }
            }
    }
    return highest >= .80 ? selected : std::nullopt;
}
bool CombatStrategy::consume(const SkillSelection &selection, SkillOutcome outcome) {
    if (outcome != SkillOutcome::Succeeded && outcome != SkillOutcome::AutoFallbackConfirmed)
        return false;
    if (selection.strategy_epoch != epoch_ || !current_.contains("skill_settings"))
        throw std::runtime_error("STALE_STRATEGY_SELECTION");
    auto &rows = current_.at("skill_settings");
    if (!rows.is_array() || selection.row >= rows.size() || rows[selection.row] != selection.skill)
        throw std::runtime_error("STALE_STRATEGY_SELECTION");
    rows.erase(rows.begin() + selection.row);
    if (current_.value("complete_one_as_all", false))
        rows.clear();
    ++epoch_;
    return true;
}
J CombatStrategy::summary() const {
    return {{"epoch", epoch_}, {"current", current_}, {"automatic", automatic()}};
}
} // namespace wvd::games
