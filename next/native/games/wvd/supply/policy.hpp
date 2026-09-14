#pragma once
#include <json.hpp>
#include <cstdint>

namespace wvd::games::supply {
enum class RestReason { None, Interval, PartyRefresh, PickaxesExhausted };
struct SupplyFacts {
    std::uint64_t dungeons{};
    bool met_encounter{};
    double total_seconds{};
    std::uint64_t last_bag_clear{};
};
struct RestDecision {
    RestReason reason{RestReason::None};
    bool reassemble{};
    bool royal_suite{};
    bool required() const { return reason != RestReason::None; }
};
RestDecision decide_rest(const nlohmann::json &profile, const SupplyFacts &facts,
                         bool pickaxes_exhausted = false);
bool ordinary_rest_due(const nlohmann::json &profile, const SupplyFacts &facts);
} // namespace wvd::games::supply
