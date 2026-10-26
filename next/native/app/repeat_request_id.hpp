#pragma once
#include "games/wvd/tasks/handoff_provenance.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace wvd::app {
// The first cycle is the external submission. Internal cycles use the exact
// watcher identity material; maintenance never infers a parent from a prefix.
inline std::string repeat_request_id(const std::string &root, std::uint64_t cycle) {
    if (root.empty() || !cycle) throw std::runtime_error("REPEAT_IDENTITY_INVALID");
    if (cycle == 1) return root;
    return "repeat-" + games::tasks::digest_handoff_json({{"session",root},{"cycle",cycle}});
}
inline bool clean_repeat_cycle(bool completed, std::uint64_t units, bool details_complete,
    const std::vector<std::string> &secondary_errors, const nlohmann::json &business) {
    const auto cycle = business.value("bounty_cycle",nlohmann::json::object());
    return completed && units == 3 &&
        std::all_of(secondary_errors.begin(),secondary_errors.end(),
            [](const auto &error) { return error == "DIAGNOSTIC_IMAGES_INCOMPLETE"; }) && details_complete &&
        cycle.value("completed_cycles",0) == 1 && cycle.value("reports_remaining",-1) == 0 &&
        !business.value("bounty_report_pending",true) && !business.value("inn_payment_pending",true);
}
}
