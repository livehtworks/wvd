#include "enemy_rules.hpp"
#include <set>
#include <stdexcept>
#include <algorithm>

namespace wvd::games::combat {
std::vector<EnemyRule> enemy_rules(const nlohmann::json &profile) {
    const auto special = profile.at("TASK_POINT_STRATEGY").value("special_combat", nlohmann::json::object());
    const auto rules = special.value("rules", nlohmann::json::array());
    if (!rules.is_array() || rules.size() > 64) throw std::runtime_error("ENEMY_RULES_INVALID");
    std::set<std::string> ids, names;
    std::vector<EnemyRule> result;
    for (const auto &rule : rules) {
        if (!rule.is_object()) throw std::runtime_error("ENEMY_RULE_INVALID");
        EnemyRule value{rule.at("id"), rule.at("name"), rule.at("portrait_image"),
            rule.at("strategy"), rule.value("portrait_png_base64", "")};
        if (value.id.empty() || value.id.size() > 64 || value.name.empty() || value.name.size() > 256 ||
            value.image.empty() || value.strategy.empty() || !ids.insert(value.id).second || !names.insert(value.name).second ||
            value.png.size() > 350000)
            throw std::runtime_error("ENEMY_RULE_INVALID");
        bool found = false;
        for (const auto &group : profile.at("STRATEGY")) found |= group.at("group_name").get<std::string>() == value.strategy;
        if (!found) throw std::runtime_error("ENEMY_RULE_STRATEGY_MISSING");
        result.push_back(std::move(value));
    }
    return result;
}
}
