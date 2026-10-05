#include "strategy.hpp"
#include "enemy_rules.hpp"
#include "games/wvd/profile.hpp"
#include <cmath>

namespace wvd::games {
using J = nlohmann::json;
std::set<std::string> reachable_strategy_groups(const J &profile) {
    std::set<std::string> result;
    const auto &points = profile.at("TASK_POINT_STRATEGY");
    const auto name = profile.at("TASK_SPECIFIC_CONFIG").get<bool>()
        ? points.value("overall_strategy", "")
        : profile.at("DEFAULT_OVERALL_STRATEGY").get<std::string>();
    const auto custom = profile.at("LANGUAGE") == "en_US" ? "Custom Task Point Strategy" : "自定义任务点策略";
    if (profile.at("TASK_SPECIFIC_CONFIG").get<bool>() && name == custom) {
        const auto mapping = points.value("task_point", J::object());
        for (const auto &value : mapping) result.insert(value.get<std::string>());
    } else result.insert(name);
    const auto special = points.value("special_combat", J::object());
    const bool portrait = special.value("portrait", false), skull = special.value("skull", false);
    if (portrait || skull) {
        result.insert(special.value("normal_strategy", ""));
        const auto rules = combat::enemy_rules(profile);
        if (skull || rules.empty()) result.insert(special.value("special_strategy", ""));
        if (portrait) for (const auto &rule : rules) result.insert(rule.strategy);
    }
    return result;
}
CombatStrategy::CombatStrategy(J profile)
    : profile_(std::move(profile)), english_(profile_.at("LANGUAGE") == "en_US") {
    validate_strategy(profile_.at("STRATEGY"));
    if (!profile_.at("TASK_SPECIFIC_CONFIG").is_boolean() ||
        !profile_.at("DEFAULT_OVERALL_STRATEGY").is_string() ||
        !profile_.at("TASK_POINT_STRATEGY").is_object())
        throw std::runtime_error("STRATEGY_PROFILE_INVALID");
    const auto special = profile_.at("TASK_POINT_STRATEGY").value("special_combat", J::object());
    if (!special.is_object()) throw std::runtime_error("SPECIAL_COMBAT_INVALID");
    const auto rules = combat::enemy_rules(profile_);
    if (!special.value("skull", false) && !special.value("portrait", false)) return;
    if (special.value("portrait", false) && rules.empty() &&
        (!special.contains("portrait_image") || !special.at("portrait_image").is_string() ||
         special.at("portrait_image").get<std::string>().empty()))
        throw std::runtime_error("SPECIAL_COMBAT_PORTRAIT_REQUIRED");
    for (const auto *field : {"normal_strategy", "special_strategy"}) {
        if (std::string(field) == "special_strategy" && !special.value("skull", false) && !rules.empty()) continue;
        if (!special.contains(field) || !special.at(field).is_string() ||
            special.at(field).get<std::string>().empty())
            throw std::runtime_error("SPECIAL_COMBAT_STRATEGY_REQUIRED");
        const auto name = special.at(field).get<std::string>();
        bool found = false;
        for (const auto &group : profile_.at("STRATEGY"))
            found |= group.value("group_name", "") == name;
        if (!found) throw std::runtime_error("SPECIAL_COMBAT_STRATEGY_MISSING");
    }
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
    load_group(key);
}
void CombatStrategy::load_group(const std::string &key) {
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
void CombatStrategy::begin_encounter(bool special, const std::string &enemy_rule) {
    const auto config = profile_.at("TASK_POINT_STRATEGY").value("special_combat", J::object());
    if (!config.value("skull", false) && !config.value("portrait", false)) return;
    if (!enemy_rule.empty()) {
        for (const auto &rule : combat::enemy_rules(profile_)) if (rule.id == enemy_rule) {
            load_group(rule.strategy);
            return;
        }
        throw std::runtime_error("ENEMY_RULE_NOT_FOUND");
    }
    load_group(config.at(special ? "special_strategy" : "normal_strategy").get<std::string>());
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
    if (outcome != SkillOutcome::Succeeded && outcome != SkillOutcome::AutoFallbackConfirmed &&
        outcome != SkillOutcome::DefendFallbackConfirmed)
        return false;
    if (selection.strategy_epoch != epoch_ || !current_.contains("skill_settings"))
        throw std::runtime_error("STALE_STRATEGY_SELECTION");
    auto &rows = current_.at("skill_settings");
    if (!rows.is_array() || selection.row >= rows.size() || rows[selection.row] != selection.skill)
        throw std::runtime_error("STALE_STRATEGY_SELECTION");
    const auto frequency = selection.skill.value("freq_var", "用完后移除");
    if (frequency != "用完后移除" && frequency != "重复")
        throw std::runtime_error("PROFILE_SKILL_FREQUENCY_INVALID:" + frequency);
    // 自动保底只确认本次角色行动，不能冒充所配置技能已施放。
    if (outcome == SkillOutcome::Succeeded) {
        if (frequency == "用完后移除") rows.erase(rows.begin() + selection.row);
        if (current_.value("complete_one_as_all", false)) rows.clear();
    }
    // 保留重复行也必须作废本次选择，防止同一回执再次结算；下一行动重新识别角色。
    ++epoch_;
    return true;
}
J CombatStrategy::summary() const {
    return {{"epoch", epoch_}, {"current", current_}, {"automatic", automatic()}};
}
} // namespace wvd::games
