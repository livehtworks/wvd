#pragma once
#include "contracts/observation_fault.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace wvd::devices {
struct AdbFailureInfo {
    std::string code, command, serial, stdout_excerpt, stderr_excerpt;
    std::chrono::milliseconds timeout{}, elapsed{};
    std::uint32_t exit_code{};
    bool timed_out{};
    bool retryable_transport{};
};
class AdbCommandFailure final : public std::runtime_error {
  public:
    explicit AdbCommandFailure(AdbFailureInfo info)
        : std::runtime_error(info.code), info_(std::move(info)) {}
    const AdbFailureInfo &info() const noexcept { return info_; }
    // 必须由已知只读调用点显式调用；通用 shell_fixed 不执行恢复或重放。
    contracts::ObservationUnavailable as_read_fault(
        contracts::ReadFaultStage stage, const std::string &operation) const {
        return contracts::ObservationUnavailable({
            info_.timed_out ? contracts::ReadFaultKind::Timeout
                           : contracts::ReadFaultKind::TransportUnavailable,
            stage, info_.code, operation, info_.timeout, info_.elapsed,
            {{"command", info_.command}, {"device", info_.serial},
             {"exit_code", info_.exit_code}, {"stdout_excerpt", info_.stdout_excerpt},
             {"stderr_excerpt", info_.stderr_excerpt}}});
    }
  private:
    AdbFailureInfo info_;
};
inline bool transient_adb_transport(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // 授权、参数和输出格式错误不会因多等一次变好，不归入可重试集合。
    if (text.find("unauthorized") != std::string::npos ||
        text.find("permission denied") != std::string::npos) return false;
    for (const char *marker : {"device offline", "device not found", "no devices/emulators",
            "connection reset", "connection closed", "connection refused", "error: closed",
            "cannot connect", "failed to connect"})
        if (text.find(marker) != std::string::npos) return true;
    return text.find("device '") != std::string::npos &&
           text.find("not found") != std::string::npos;
}
} // namespace wvd::devices
