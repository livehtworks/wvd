#pragma once
#include <json.hpp>
#include <set>
#include <string>

namespace wvd::games {
// 业务条件仅比较稳定的标量摘要，不把配置/状态变成可执行表达式或第二套流程语言。
inline bool business_condition(const nlohmann::json &summary, const nlohmann::json &parameters) {
    static const std::set<std::string> fields{
        "/task_step", "/pending_combat", "/pending_chest", "/need_initial_recover",
        "/recover_after_rez", "/met_encounter", "/dungeons", "/combats", "/chests", "/strategy/automatic",
        "/has_prepared_skill", "/prepared_skill_index", "/healing_required", "/chest_has_character", "/chest_character",
        "/ordinary_rest_due", "/party_refresh_due", "/city_supply_due", "/inn_rest_completed"};
    const auto path = parameters.at("field").get<std::string>();
    if (!fields.contains(path))
        throw std::runtime_error("BUSINESS_CONDITION_FIELD_INVALID");
    const auto &actual = summary.at(nlohmann::json::json_pointer(path));
    const auto &expected = parameters.at("value");
    const auto comparison = parameters.value("comparison", "eq");
    if ((path == "/prepared_skill_index" || path == "/chest_character") && actual.is_null())
        return false;
    if (actual.is_boolean()) {
        if (!expected.is_boolean() || comparison != "eq")
            throw std::runtime_error("BUSINESS_CONDITION_TYPE_INVALID");
        return actual == expected;
    }
    if (!actual.is_number_integer() || !expected.is_number_integer() || expected < 0 ||
        (comparison != "eq" && comparison != "gte"))
        throw std::runtime_error("BUSINESS_CONDITION_TYPE_INVALID");
    return comparison == "eq" ? actual == expected : actual >= expected;
}
}
