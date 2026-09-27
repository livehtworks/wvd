#pragma once
#include "contracts/observation_fault.hpp"
#include <cmath>

namespace wvd::devices {
// 只分类单次查询报告。重试、退避与剩余预算仍由原FlowExecutor拥有。
inline void require_metadata_read(const nlohmann::json &report, std::chrono::milliseconds budget) {
    if (report.value("success", false)) return;
    const auto primary = report.value("primary_error", std::string{});
    const auto error = report.value("error", std::string{});
    const bool cleaned = report.value("quiescent", false) && report.value("handles_released", false) &&
        report.value("helper_exited", false) && report.contains("pending_io") && report.at("pending_io") == 0 &&
        error != "METADATA_CLEANUP_PENDING";
    const auto elapsed = report.value("elapsed_ms", -1.0);
    // 缺清理证明、非法耗时、取消、身份/JSON错误不能落入超时重试。
    if (primary == "METADATA_TIMEOUT" && error == primary && cleaned &&
        std::isfinite(elapsed) && elapsed >= 0 && elapsed <= 3600000) {
        const auto raw = report.dump();
        throw contracts::ObservationUnavailable({contracts::ReadFaultKind::Timeout,
            contracts::ReadFaultStage::Capture, primary, "mumu.instance_metadata", budget,
            std::chrono::milliseconds{static_cast<std::int64_t>(elapsed)},
            {{"primary_error", primary}, {"quiescent", true}, {"handles_released", true},
             {"helper_exited", true}, {"pending_io", 0}, {"budget_ms", budget.count()},
             {"elapsed_ms", elapsed}, {"report", raw.substr(0, 8192)}, {"report_truncated", raw.size() > 8192}}});
    }
    throw std::runtime_error((error.empty() ? "MUMU_METADATA_UNAVAILABLE" : error) +
        (primary.empty() || primary == error ? "" : ":" + primary));
}
} // namespace wvd::devices
