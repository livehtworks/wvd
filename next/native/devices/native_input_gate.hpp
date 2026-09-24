#pragma once
#include "backend.hpp"
#include <atomic>
#include <mutex>
#include <stop_token>

namespace wvd::devices {
enum class InputDisposition { Submitted, Rejected, Unresolved };
struct InputReceipt {
    InputDisposition disposition{InputDisposition::Rejected};
    std::uint64_t action_epoch{};
    std::chrono::steady_clock::time_point submitted_at{};
    std::string detail;
};

// Only the physical-input boundary lives here. Workflow receipts, event scopes,
// and result deadlines belong exclusively to FlowExecutor.
class NativeInputGate final {
  public:
    NativeInputGate(DeviceBackend &backend, contracts::InputPolicy policy,
                    std::uint64_t generation);
    contracts::FrameEnvelope capture();
    InputReceipt submit(const contracts::Command &command,
                        const contracts::Observation &scene,
                        const contracts::Observation &target,
                        const contracts::Box &allowed_area);
    void stop();
    bool stopped() const { return stopped_.load(); }
    bool cleanup();
    bool current(const contracts::FrameIdentity &identity) const;
    contracts::FrameIdentity current_identity() const;
    std::uint64_t action_epoch() const;
    contracts::InputCounts counts() const;

  private:
    contracts::Command map_command(const contracts::Command &command,
                                   const contracts::FrameIdentity &basis) const;
    DeviceBackend &backend_;
    contracts::InputPolicy policy_;
    std::uint64_t generation_{};
    std::atomic<bool> stopped_{false};
    std::stop_source stop_source_;
    mutable std::mutex mutex_;
    // 只串行真实取帧/提交；stop() 不拿此锁，不等待设备 I/O。
    std::mutex dispatch_mutex_;
    contracts::FrameIdentity last_frame_;
    std::uint64_t frame_id_{}, epoch_{};
    contracts::InputCounts counts_;
};
} // namespace wvd::devices
