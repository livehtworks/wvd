#include "profile.hpp"
#include <set>

namespace wvd::games {
namespace {
void typed(const nlohmann::json &object, const char *field, nlohmann::json::value_t type) {
    if (!object.contains(field))
        return;
    const auto &value = object.at(field);
    if (type == nlohmann::json::value_t::number_integer ? !value.is_number_integer()
                                                        : value.type() != type)
        throw std::runtime_error(std::string("PROFILE_FIELD_TYPE:") + field);
}
} // namespace
void validate_strategy(const nlohmann::json &strategy) {
    using T = nlohmann::json::value_t;
    if (!strategy.is_array())
        throw std::runtime_error("PROFILE_STRATEGY_TYPE");
    for (const auto &group : strategy) {
        if (!group.is_object())
            throw std::runtime_error("PROFILE_STRATEGY_GROUP_TYPE");
        typed(group, "group_name", T::string);
        typed(group, "skill_settings", T::array);
        typed(group, "complete_one_as_all", T::boolean);
        if (group.contains("skill_settings"))
            for (const auto &row : group.at("skill_settings")) {
                if (!row.is_object())
                    throw std::runtime_error("PROFILE_SKILL_ROW_TYPE");
                for (const auto *key : {"role_var", "skill_var", "target_var", "freq_var"})
                    typed(row, key, T::string);
                typed(row, "skill_lvl", T::number_integer);
            }
    }
}
void normalize_strategy(nlohmann::json &strategy) {
    validate_strategy(strategy);
    static const std::set<std::string> targets{"左上角色", "中上角色", "右上角色", "左下角色", "中下角色", "右下角色"};
    for (auto &group : strategy) {
        if (!group.contains("skill_settings")) continue;
        for (auto &row : group["skill_settings"]) {
            auto target = row.value("target_var", "");
            if (target.empty() || target == "低生命值" || target == "不可用" || target == "默认" ||
                target == "Low HP" || target == "Unavailable" || target == "next") target = "左上角色";
            if (!targets.contains(target)) throw std::runtime_error("PROFILE_SKILL_TARGET_INVALID:" + target);
            auto frequency = row.value("freq_var", "");
            if (frequency == "repeat" || frequency == "Repeat" || frequency == "重复该动作") frequency = "重复";
            if (frequency.empty() || frequency == "每场战斗仅一次" || frequency == "每次启动仅一次" ||
                frequency == "Once per battle" || frequency == "Once per start") frequency = "用完后移除";
            if (frequency != "重复" && frequency != "用完后移除")
                throw std::runtime_error("PROFILE_SKILL_FREQUENCY_INVALID:" + frequency);
            row["target_var"] = target;
            row["freq_var"] = frequency;
        }
    }
}
} // namespace wvd::games
