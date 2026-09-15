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
        "/ordinary_rest_due", "/party_refresh_due", "/city_supply_due", "/inn_rest_completed", "/inn_payment_pending", "/death_prompt_pending", "/wall_bypass_step",
        "/giant_route_completed", "/giant_rest_due", "/giant_cycle_active",
        "/dark_light_active", "/encounter_timed_out", "/unit_index",
        "/manual_separation/phase", "/manual_separation/transfer_pending",
        "/bounty_report_pending",
        "/fishing/reward_pending",
        "/fishing/waiting", "/fishing/timed_out",
        "/fishing/casting_pending",
        "/sleep/batch_complete", "/sleep/visit_active",
        "/bounty_cycle/active", "/bounty_cycle/phase", "/bounty_cycle/unit_matches",
        "/bounty_cycle/rest_due", "/bounty_cycle/transfer_pending", "/bounty_cycle/reports_remaining",
        "/mining/refill_pending", "/mining/party_ready", "/mining/reward_visible",
        "/karma_ambush", "/karma_pending"};
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
