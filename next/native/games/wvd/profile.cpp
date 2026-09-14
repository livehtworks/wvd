#include "profile.hpp"

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
} // namespace wvd::games
