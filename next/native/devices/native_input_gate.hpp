#pragma once
#include "backend.hpp"
#include "contracts/observation_fault.hpp"
#include <optional>
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
    // 仅 Rejected 且尚未进入输入后端时填写，不能由此重放 Unresolved 输入。
    std::optional<contracts::ReadFault> read_fault;
};

// Only the physical-input boundary lives here. Workflow receipts, event scopes,
// and result deadlines belong exclusively to FlowExecutor.
class NativeInputGate final {
  public:
    NativeInputGate(DeviceBackend &backend, contracts::InputPolicy policy,
                    std::uint64_t generation);
    contracts::FrameEnvelope capture();
    // 返回 true 表示通道已重建，原观察已撤销，调用者必须重新取帧。
    bool prepare_for_input();
    void observation_window(std::chrono::steady_clock::time_point deadline) {
        backend_.observation_window(deadline, stop_source_.get_token());
    }
    InputReceipt submit(const contracts::Command &command,
                        const contracts::Observation &scene,
                        const contracts::Observation &target,
                        const contracts::Box &allowed_area);
    void stop();
    bool stopped() const { return stopped_.load(); }
    bool cleanup();
    bool current(const contracts::FrameIdentity &identity) const;
    bool reusable(const contracts::FrameIdentity &identity) const;
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
