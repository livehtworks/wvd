#pragma once

#include "contracts/action.hpp"
#include "workflow/program.hpp"
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wvd::runtime {
enum class SubmissionState { Accepted, Rejected, Unresolved };
struct Submission {
    SubmissionState state{SubmissionState::Rejected};
    std::uint64_t action_epoch{};
    std::chrono::steady_clock::time_point submitted_at{};
    std::string detail;
};
enum class OperationState { Done, Waiting, Failed, ExternalBlocked };
struct OperationResult {
    OperationState state{OperationState::Failed};
    std::string detail;
    std::chrono::steady_clock::time_point wake_at{};
};

// 运行层依赖能力端口，不依赖 WVD 的页面名、素材路径或设备 SDK。
class FlowPorts {
  public:
    virtual ~FlowPorts() = default;
    virtual contracts::FrameEnvelope capture() = 0;
    virtual contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                              const recognition::Request &request) = 0;
    virtual Submission submit(const contracts::Command &command,
                              const contracts::Observation &scene,
                              const contracts::Observation &target,
                              contracts::Box allowed_area,
                              const std::string &source_path) = 0;
    virtual OperationResult operate(const std::string &binding,
                                    const nlohmann::json &parameters,
                                    const std::optional<contracts::FrameEnvelope> &frame,
                                    const std::optional<contracts::Observation> &observation,
                                    const std::string &source_path) = 0;
    virtual bool cancelled() const = 0;
    // 未实现复用能力的端口始终重新观察；正式端口还检查输入门禁的身份和 TTL。
    virtual bool reusable(const contracts::FrameIdentity &) const { return false; }
};
enum class TickState { Progress, Waiting, Completed, BusinessFailed, Failed, ExternalBlocked, Cancelled };
struct TickResult {
    TickState state{TickState::Failed};
    std::chrono::steady_clock::time_point wake_at{};
    std::string code;
    std::string source_path;
};

// 只有 Session 的工作线程调用 tick；停止线程只操作端口取消标记。
// 公共调用和额外事件共用该栈，不启动第二个执行器/线程。
class FlowExecutor final {
  public:
    FlowExecutor(const workflow::FlowProgram &program, FlowPorts &ports,
                 std::chrono::milliseconds total_budget);
    TickResult tick();
    const std::string &current_source_path() const;
    std::string current_step_id() const;
    nlohmann::json progress_snapshot() const;
    std::size_t invocation_depth() const { return stack_.size(); }
    bool has_unresolved_input() const;
    bool is_operation(const std::string &binding, const std::string &operation) const;
    bool operation_advanced() const { return !stack_.empty() && stack_.back().next_pending && !stack_.back().error_pending; }

  private:
    using Clock = std::chrono::steady_clock;
    struct PendingInput {
        std::string source_path;
        contracts::FrameIdentity before;
        std::uint64_t action_epoch{};
        Clock::time_point submitted_at;
        Clock::duration event_pause{};
        std::optional<recognition::Request> expected_result;
        std::chrono::milliseconds result_budget{0};
        // Unresolved 不是未发送：传输是否送达未知也必须阻止重发/恢复。
        bool delivery_unknown{};
    };
    struct EventExit {
        std::string id;
        recognition::Request detect;
        Clock::time_point deadline;
    };
    struct ScopedEvent {
        workflow::EventRule rule;
        std::size_t owner{}; // 声明规则的实际调用帧，不是一个裸节点名。
    };
    struct PendingResume {
        workflow::EventRule rule;
        std::size_t owner{};
    };
    struct Frame {
        struct InputSelection {
            std::string predecessor;
            Clock::time_point entered_at;
            bool error_pending{};
        };
        std::string definition;
        std::string current;
        std::map<std::string, int> hits;
        std::map<std::string, Clock::time_point> phase_deadlines;
        Clock::time_point invoked_at;
        std::optional<Clock::time_point> invocation_deadline;
        Clock::time_point entered_at;
        Clock::time_point next_event_poll{};
        std::optional<Clock::time_point> delay_until;
        Clock::duration paused_event_time{};
        bool next_pending{};
        bool error_pending{};
        std::optional<contracts::FrameEnvelope> selected_frame;
        std::optional<contracts::Observation> selected_observation;
        std::optional<InputSelection> input_selection;
        std::optional<ScopedEvent> event;
        std::optional<PendingInput> pending;
        std::vector<EventExit> event_exits;
        std::optional<PendingResume> resume;
        std::optional<Clock::time_point> ambiguity_since;
        int ambiguity_priority{};
        workflow::EventClass ambiguity_category{workflow::EventClass::Overlay};
    };
    const workflow::FlowProgram &program_;
    FlowPorts &ports_;
    const Clock::time_point deadline_; // 总墙钟期限永不因事件或调用重置。
    Clock::time_point accounted_at_;
    std::vector<Frame> stack_;
    TickResult terminal_{TickState::Progress};
    struct ObservationCycle {
        contracts::FrameEnvelope frame;
        std::optional<std::string> clear_overlay_scope;
    };
    std::optional<ObservationCycle> observation_cycle_;
    contracts::FrameEnvelope observation_frame();
    void invalidate_observation();

    const workflow::Step &step() const;
    TickResult progress() const;
    TickResult waiting(std::chrono::milliseconds delay);
    TickResult reconsider_unsubmitted_input(Frame &frame);
    TickResult fail(std::string code);
    TickResult business_fail(std::string reason, std::string source);
    TickResult return_business_failure(const std::string &reason);
    TickResult blocked(std::string code);
    TickResult route_error(Frame &frame, const workflow::Step &current, std::string code);
    TickResult select_next(Frame &frame, const workflow::Step &current);
    TickResult execute_step(Frame &frame, const workflow::Step &current);
    TickResult resume_event(Frame &frame, const workflow::Step &current);
    std::optional<TickResult> check_events(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image, workflow::EventClass category);
    std::optional<TickResult> poll_wait_events(Frame &frame, const workflow::Step &current);
    std::vector<ScopedEvent> effective_events(const workflow::Step &current) const;
    // 按暂停区间并集记账，嵌套事件不得重复延长期限；覆盖所有祖先帧。
    void account_event_time();
    void advance(Frame &frame);
    contracts::Command command(const workflow::Input &input,
                                const contracts::Observation &target) const;
};
} // namespace wvd::runtime
