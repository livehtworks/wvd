#pragma once
#include "backend.hpp"
#include "storage/run_store.hpp"
#include <atomic>
#include <optional>

namespace wvd::devices {
class InputGate {
  public:
    InputGate(DeviceBackend &backend, contracts::InputPolicy policy, std::uint64_t run,
              std::uint64_t generation, storage::EventJournal &events);
    bool connect();
    RawFrame capture();
    contracts::FrameIdentity frame_identity() const;
    void confirm_scene(const contracts::Observation &observation, const std::string &scene);
    bool authorize(const contracts::ActionIntent &intent);
    void revoke();
    bool execute(const contracts::Command &command);
    void close();
    bool closed() const {
        return closed_.load();
    }
    bool release_held();
    bool quiescent() const;
    contracts::InputCounts counts() const;
    const contracts::InputPolicy &policy() const {
        return policy_;
    }
    std::uint64_t generation() const {
        return generation_;
    }
    std::uint64_t run_id() const {
        return run_;
    }

  private:
    bool same_frame(const contracts::FrameIdentity &frame) const;
    std::string reject_reason(const contracts::Command &command) const;
    contracts::Command mapped(const contracts::Command &command) const;
    DeviceBackend &backend_;
    const contracts::InputPolicy policy_;
    const std::uint64_t run_, generation_;
    storage::EventJournal &events_;
    std::atomic<bool> closed_{false};
    // 生命周期门锁绝不跨底层调用；close 不等待 dispatch_mutex 中的原生阻塞。
    mutable std::mutex mutex_;
    std::mutex dispatch_mutex_;
    contracts::FrameIdentity frame_;
    std::string application_, scene_;
    std::optional<contracts::ActionIntent> permit_;
    contracts::InputCounts counts_;
    std::set<int> touches_, keys_;
    std::size_t in_flight_{};
    std::uint64_t next_frame_{}, epoch_{};
};
} // namespace wvd::devices
