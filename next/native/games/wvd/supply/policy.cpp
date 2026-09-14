#include "policy.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wvd::games::supply {
bool ordinary_rest_due(const nlohmann::json &profile, const SupplyFacts &facts) {
    const auto interval = std::max<std::int64_t>(profile.at("REST_INTERVEL").get<std::int64_t>(), 1);
    // Python 的负数取模：初始零轮仅 interval=1 时满足，不做无符号减法。
    const bool due = facts.dungeons == 0 ? interval == 1 : (facts.dungeons - 1) % interval == 0;
    return profile.at("ACTIVE_REST").get<bool>() && facts.met_encounter && due;
}
RestDecision decide_rest(const nlohmann::json &profile, const SupplyFacts &facts,
                         bool pickaxes_exhausted) {
    if (!std::isfinite(facts.total_seconds) || facts.total_seconds < 0 ||
        facts.total_seconds / 21600 >=
            static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        throw std::runtime_error("SUPPLY_TIME_INVALID");
    const bool party =
        profile.at("RE_ASSEMBLE_PARTY").get<bool>() &&
        static_cast<std::uint64_t>(facts.total_seconds / 21600) != facts.last_bag_clear;
    const bool ordinary = ordinary_rest_due(profile, facts);
    return {pickaxes_exhausted ? RestReason::PickaxesExhausted
            : party            ? RestReason::PartyRefresh
            : ordinary         ? RestReason::Interval
                               : RestReason::None,
            party, profile.at("ACTIVE_ROYALSUITE_REST").get<bool>()};
}
} // namespace wvd::games::supply
