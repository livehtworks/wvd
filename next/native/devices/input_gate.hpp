#pragma once
#include "backend.hpp"
#include "storage/run_store.hpp"
#include <atomic>
#include <optional>
#include <thread>
#include <vector>

namespace wvd::devices {
class InputGate {
  public:
    InputGate(DeviceBackend &backend, contracts::InputPolicy policy, std::uint64_t run,
              std::uint64_t generation, storage::EventJournal &events);
    ~InputGate();
    bool connect();
    RawFrame capture();
    void invalidate_frame();
    contracts::FrameIdentity frame_identity() const;
    // 结果确认只核对证据归属；可选输入寿命单独检查。
    bool input_observation_current(const contracts::Observation &observation) const;
    void begin_submission(const std::string &source_node, std::int64_t task_id,
                          const contracts::ActionIntent &intent, const std::string &condition);
    void finish_submission(bool accepted);
    contracts::SubmittedInput pending_submission(const std::string &source_node,
                          std::int64_t task_id, const std::string &condition) const;
    bool confirm_transition(const contracts::SubmittedInput &receipt,
                            const contracts::Observation &observation);
    bool has_pending_submission() const;
    std::uint64_t begin_event_scope(std::int64_t parent_task, const std::string &event_id,
                                    const std::set<std::string> &handler_nodes);
    void end_event_scope(std::uint64_t token);
    std::optional<contracts::SubmittedInput> settle_event_replan(
        std::int64_t parent_task, const contracts::Observation &guard);
    std::size_t event_depth() const;
    void begin_observation_phase(std::int64_t task_id, const std::string &phase,
                                 std::chrono::milliseconds budget);
    void end_observation_phase(std::int64_t task_id, const std::string &phase);
    std::chrono::steady_clock::time_point observation_deadline() const;
    bool current_observation(const contracts::Observation &observation) const;
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
    void disconnect_backend() {
        backend_.disconnect();
    }
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
    bool fresh_for_input(const contracts::FrameIdentity &frame) const;
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
    std::optional<contracts::SubmittedInput> submission_;
    struct EventScope {
        std::uint64_t token{};
        std::int64_t parent_task{};
        std::string event_id;
        std::set<std::string> handler_nodes;
        std::optional<contracts::SubmittedInput> receipt;
        bool receipt_interrupted{};
        std::chrono::steady_clock::time_point entered_at;
        std::set<std::pair<std::int64_t, std::string>> paused_phases;
        std::thread::id owner;
    };
    std::vector<EventScope> event_scopes_;
    std::uint64_t next_event_token_{};
    bool submission_interrupted_{};
    std::chrono::steady_clock::time_point resume_after_{};
    // 嵌套子流程继承当前活动阶段最早截止时间；循环入口不重新开始计时。
    std::map<std::pair<std::int64_t, std::string>, std::chrono::steady_clock::time_point> observation_phases_;
    contracts::InputCounts counts_;
    std::set<int> touches_, keys_;
    std::size_t in_flight_{};
    std::uint64_t next_frame_{}, epoch_{};
};
} // namespace wvd::devices
