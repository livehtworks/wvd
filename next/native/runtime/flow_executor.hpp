#pragma once

#include "contracts/action.hpp"
#include "contracts/observation_fault.hpp"
#include "workflow/program.hpp"
#include <chrono>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wvd::runtime {
enum class SubmissionState { Accepted, Rejected, Unresolved };
struct Submission {
    SubmissionState state{SubmissionState::Rejected};
    std::uint64_t action_epoch{};
    std::chrono::steady_clock::time_point submitted_at{};
    std::string detail;
    std::optional<contracts::ReadFault> read_fault;
};
// NotApplicable guarantees that no effect has started; Waiting does not.
enum class OperationState { Done, Waiting, NotApplicable, Failed, ExternalBlocked };
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
    // 只准备本工具的输入通道，不发送游戏操作。通道改变时必须重新观察。
    virtual bool prepare_input() { return false; }
    virtual void observation_window(std::chrono::steady_clock::time_point) {}
    virtual contracts::ObservationRecovery recover_observation(bool restart_application = false) { return {}; }
    virtual bool settle_observed_input() { return true; }
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
    // 仅通知已声明正面场景命中；不取帧、不发输入、不推进游戏业务账目。
    virtual void scene_observed(const contracts::Observation &) {}
    // 诊断旁路只记录事实，不参与门禁、重试或业务成功判定。
    virtual void input_result(const nlohmann::json &) noexcept {}
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
                 std::chrono::milliseconds total_budget,
                 contracts::ObservationRecoveryPolicy observation_policy = {});
    TickResult tick();
    const std::string &current_source_path() const;
    std::string current_step_id() const;
    nlohmann::json progress_snapshot() const;
    bool progress_changed();
    std::size_t invocation_depth() const { return stack_.size(); }
    bool has_unresolved_input() const;
    bool is_operation(const std::string &binding, const std::string &operation) const;
    bool operation_advanced() const { return !stack_.empty() && stack_.back().next_pending && !stack_.back().error_pending; }
    void report_unconfirmed_inputs(const std::string &reason) noexcept;

  private:
    using Clock = std::chrono::steady_clock;
    struct SelectionOrigin {
        std::string predecessor;
        Clock::time_point entered_at;
        bool error_pending{};
        // Preserve the caller's handoff candidates, including a single-candidate handoff.
        // Restoring only predecessor would silently fall back to Call.next (success).
        std::optional<std::vector<std::string>> returned_targets;
    };
    struct PendingInput {
        enum class Delivery { Prepared, Attempted, Sent, DeliveryUnknown, Confirmed };
        std::string source_path;
        contracts::FrameIdentity before;
        std::uint64_t action_epoch{};
        Clock::time_point submitted_at;
        // 事件与只读故障暂停区间的并集；原提交时间保持不变。
        Clock::duration event_pause{};
        std::optional<recognition::Request> expected_result;
        std::chrono::milliseconds result_budget{0};
        // Unresolved 不是未发送：传输是否送达未知也必须阻止重发/恢复。
        bool delivery_unknown{};
        std::string submitted_step;
        Clock::time_point result_started_at;
        Clock::duration delay_pause_base{};
        unsigned attempts{1};
        Clock::duration animation_pause{};
        // 仅用于已声明可重试菜单在应用重启后的重新选路；不重执行前驱动作。
        std::optional<SelectionOrigin> selection_origin;
        Delivery delivery{Delivery::Sent};
    };
    struct EventExit {
        std::string id;
        recognition::Request detect;
        Clock::time_point deadline;
    };
    void report_input_result(const PendingInput &, const char *outcome,
                             std::uint64_t observed_frame = 0, const std::string &reason = {}) noexcept;
    struct ScopedEvent {
        workflow::EventRule rule;
        std::size_t owner{}; // 声明规则的实际调用帧，不是一个裸节点名。
    };
    struct PendingResume {
        workflow::EventRule rule;
        std::size_t owner{};
    };
    struct ConfirmedInputResult {
        std::string classification, source_path, source_definition;
        contracts::FrameIdentity basis;
        std::uint64_t action_epoch{};
    };
    struct Frame {
        std::string definition;
        std::string current;
        std::map<std::string, int> hits;
        std::set<std::string> timeout_reclassifications;
        std::map<std::string, Clock::time_point> phase_deadlines;
        Clock::time_point invoked_at;
        std::optional<Clock::time_point> invocation_deadline;
        Clock::time_point entered_at;
        Clock::time_point next_event_poll{};
        std::optional<Clock::time_point> no_progress_since;
        Clock::time_point next_diagnostic_poll{};
        bool diagnostic_checked{};
        bool known_wait{};
        bool operation_started{};
        std::optional<Clock::time_point> delay_until;
        // 轮询自身的唤醒时间；不覆盖 entered_at、调用累计期限或父输入回执。
        std::optional<Clock::time_point> poll_until;
        Clock::duration paused_event_time{};
        bool next_pending{};
        bool error_pending{};
        std::optional<std::vector<std::string>> returned_targets;
        std::optional<contracts::FrameEnvelope> selected_frame;
        std::optional<contracts::Observation> selected_observation;
        std::optional<SelectionOrigin> selection_origin;
        std::optional<ScopedEvent> event;
        std::optional<PendingInput> pending;
        // A single acknowledged result, scoped to this invocation; no historical pixels.
        std::optional<ConfirmedInputResult> confirmed_result;
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
    nlohmann::json last_diagnostic_ = nullptr;
    nlohmann::json last_selection_ = nullptr;
    struct ProgressRow {
        const workflow::Step *step{};
        std::uint64_t epoch{}, connection{};
        unsigned attempts{}, flags{}, exits{};
        int delivery{-1};
        std::size_t event_owner{};
        bool operator==(const ProgressRow &) const = default;
    };
    std::array<ProgressRow, 8> reported_rows_{};
    std::array<std::string, 8> reported_events_{};
    std::array<std::optional<std::vector<std::string>>, 8> reported_targets_{};
    std::array<std::string, 5> reported_diagnostic_{};
    std::string reported_recovery_code_;
    std::size_t reported_depth_{9};
    unsigned reported_recovery_flags_{}, reported_recovery_failures_{};
    TickState reported_terminal_{TickState::Progress};
    struct ReadRecovery {
        Clock::time_point started, next_attempt;
        unsigned failures{};
        contracts::ReadFault last;
        std::chrono::milliseconds outage_limit{};
        bool suspended{true};
        // 输入前查询失败后，成功截一张图还不足以宣称该查询已恢复。
        bool awaiting_input_validation{};
        bool device_checked{};
        bool restart_application{};
        bool application_restarted_in_window{};
        bool instance_restarted_in_window{};
        nlohmann::json context_recovery = nullptr;
        std::string source_path;
        std::size_t stack_depth{};
    };
    contracts::ObservationRecoveryPolicy observation_policy_;
    std::optional<ReadRecovery> read_recovery_;
    nlohmann::json last_read_recovery_ = nullptr;
    std::optional<contracts::InstanceExitProof> instance_exit_proof_;
    std::vector<contracts::ObservationReconnect> reconnects_;
    bool restart_handler_pending_{};
    bool exception_restart_supported_{};
    std::optional<Clock::time_point> exception_since_;
    bool result_identity_matches(const contracts::FrameIdentity &, const contracts::FrameIdentity &) const;
    void begin_observation_recovery(const contracts::ReadFault &fault);
    void finish_observation_recovery(const char *outcome);
    TickResult observation_retry_wait();
    TickResult retry_observation();
    nlohmann::json observation_recovery_snapshot() const;
    contracts::FrameEnvelope observation_frame();
    void invalidate_observation();

    const workflow::Step &step() const;
    TickResult progress() const;
    TickResult waiting(std::chrono::milliseconds delay);
    TickResult reconsider_uncommitted_selection(Frame &frame);
    contracts::Observation recognize_result(const contracts::FrameEnvelope &, const recognition::Request &);
    void consume_result(const recognition::Request &);
    TickResult fail(std::string code);
    TickResult business_fail(std::string reason, std::string source);
    TickResult return_business_failure(const std::string &reason);
    TickResult blocked(std::string code);
    TickResult route_error(Frame &frame, const workflow::Step &current, std::string code);
    std::optional<TickResult> reclassify_timeout(Frame &frame, const workflow::Step &current,
                                                  const std::string &code);
    TickResult select_next(Frame &frame, const workflow::Step &current);
    void record_known_scene(Frame &frame, const contracts::Observation &observed);
    TickResult execute_step(Frame &frame, const workflow::Step &current);
    // 普通等待、到期宽限与重启前复核共用唯一回执结算，不在这里补发输入。
    std::optional<TickResult> settle_await_result(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image);
    std::optional<TickResult> recheck_normal_observation(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image);
    std::optional<TickResult> recheck_expired_poll(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image);
    std::optional<TickResult> retry_pending_input(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image);
    TickResult resume_event(Frame &frame, const workflow::Step &current);
    std::optional<TickResult> check_events(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image, workflow::EventClass category);
    std::optional<TickResult> poll_wait_events(Frame &frame, const workflow::Step &current);
    // 仅在当前业务的候选/场景/目标/后置条件不符时调用；不把正常动画当成失败。
    std::optional<TickResult> check_unexpected(Frame &frame, const workflow::Step &current,
        const contracts::FrameEnvelope &image, const std::string &reason, bool force = false);
    struct EventScopeKey {
        const workflow::Step *step{};
        std::string active;
    };
    mutable std::vector<EventScopeKey> event_scope_keys_;
    mutable std::vector<ScopedEvent> event_scope_rules_;
    mutable std::uint64_t event_scope_version_{};
    const std::vector<ScopedEvent> &effective_events(const workflow::Step &current) const;
    // 按暂停区间并集记账，嵌套事件不得重复延长期限；覆盖所有祖先帧。
    void account_event_time();
    // observed_progress=false 用于纯控制、等待及阶段标记，不能清除未知现场历史。
    void advance(Frame &frame, bool observed_progress = true);
    void clear_unexpected(Frame &frame);
    contracts::Command command(const workflow::Input &input,
                                const contracts::Observation &target) const;
};
} // namespace wvd::runtime
