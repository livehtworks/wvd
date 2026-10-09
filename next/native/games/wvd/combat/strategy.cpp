#include "strategy.hpp"
#include "enemy_rules.hpp"
#include "games/wvd/profile.hpp"
#include <cmath>

namespace wvd::games {
using J = nlohmann::json;
namespace {
bool task_point_mode(const std::string &name) {
    return name == "自定义任务点策略" || name == "Custom Task Point Strategy";
}
bool auto_mode(const std::string &name) { return name == "全自动战斗" || name == "Full Auto"; }
std::string require_group(const J &profile, const std::string &name) {
    if (name.empty()) throw std::runtime_error("STRATEGY_DEFAULT_REQUIRED");
    for (const auto &group : profile.at("STRATEGY")) {
        const auto candidate = group.value("group_name", "");
        if (candidate == name || (auto_mode(name) && auto_mode(candidate))) return candidate;
    }
    throw std::runtime_error("STRATEGY_GROUP_NOT_FOUND:" + name);
}
}
std::string effective_strategy_name(const J &profile, std::optional<std::size_t> task_step) {
    const auto global = profile.at("DEFAULT_OVERALL_STRATEGY").get<std::string>();
    const auto &points = profile.at("TASK_POINT_STRATEGY");
    auto selected = profile.at("TASK_SPECIFIC_CONFIG").get<bool>() ? points.value("overall_strategy", "") : global;
    if (selected.empty()) selected = global;
    if (task_point_mode(selected)) {
        if (!task_step) return "Custom Task Point Strategy";
        selected = points.value("task_point", J::object()).value(std::to_string(*task_step), "");
        if (selected.empty()) selected = global;
    }
    return require_group(profile, selected);
}
std::set<std::string> reachable_strategy_groups(const J &profile) {
    std::set<std::string> result;
    const auto &points = profile.at("TASK_POINT_STRATEGY");
    const auto name = effective_strategy_name(profile);
    if (task_point_mode(name)) {
        const auto mapping = points.value("task_point", J::object());
        result.insert(require_group(profile, profile.at("DEFAULT_OVERALL_STRATEGY")));
        for (const auto &[key,value] : mapping.items()) {
            const auto group = value.get<std::string>();
            if (!group.empty()) result.insert(require_group(profile,group));
        }
    } else result.insert(name);
    const auto special = points.value("special_combat", J::object());
    const bool portrait = special.value("portrait", false), skull = special.value("skull", false);
    if (portrait || skull) {
        result.insert(require_group(profile,special.value("normal_strategy", "")));
        const auto rules = combat::enemy_rules(profile);
        if (skull || rules.empty()) result.insert(require_group(profile,special.value("special_strategy", "")));
        if (portrait) for (const auto &rule : rules) result.insert(require_group(profile,rule.strategy));
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
    (void)reachable_strategy_groups(profile_);
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
    return task_point_mode(effective_strategy_name(profile_));
}
void CombatStrategy::reload(std::size_t task_step) {
    load_group(effective_strategy_name(profile_,task_step));
}
void CombatStrategy::load_group(const std::string &key) {
    current_ = J::object();
    // 固定旧源选择首个同名分组；深复制后消费，不修改冻结配置。
    const auto resolved = require_group(profile_, key);
    for (const auto &group : profile_.at("STRATEGY")) {
        if (group.value("group_name", "") == resolved) {
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
           auto_mode(current_.value("group_name", "")) ||
           current_.value("skill_settings", J::array()).empty();
}
std::optional<SkillSelection>
CombatStrategy::select(const std::vector<PortraitScore> &scores) const {
    if (automatic())
        return std::nullopt;
    double highest = 0;
    const PortraitScore *identity = nullptr;
    for (const auto &score : scores) {
        if (!std::isfinite(score.score) || score.score < -1 || score.score > 1)
            throw std::runtime_error("PORTRAIT_SCORE_INVALID");
        if (score.score > highest) { highest = score.score; identity = &score; }
    }
    if (!identity || highest < .80) return std::nullopt;
    const auto &rows = current_.at("skill_settings");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto role = rows[i].value("role_var", "");
        if (role.empty())
            continue;
        for (const auto &candidate : {role, role + "_sp", role + "_alt"})
            if (identity->portrait == candidate) return SkillSelection{"", 0, epoch_, i, rows[i]};
    }
    return std::nullopt;
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
