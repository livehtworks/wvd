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
};

enum class TickState { Progress, Waiting, Completed, Failed, ExternalBlocked, Cancelled };
struct TickResult {
    TickState state{TickState::Failed};
    std::chrono::steady_clock::time_point wake_at{};
    std::string code;
    std::string source_path;
};

class FlowExecutor final {
  public:
    FlowExecutor(const workflow::FlowProgram &program, FlowPorts &ports,
                 std::chrono::milliseconds total_budget);
    TickResult tick();
    const std::string &current_source_path() const;
    std::string current_step_id() const;
    std::size_t invocation_depth() const { return stack_.size(); }
    bool has_unresolved_input() const;

  private:
    struct PendingInput {
        std::string source_path;
        contracts::FrameIdentity before;
        std::uint64_t action_epoch{};
        std::chrono::steady_clock::time_point submitted_at;
        std::chrono::steady_clock::duration event_pause{};
    };
    struct EventExit {
        std::string id;
        recognition::Request detect;
        std::chrono::steady_clock::time_point deadline;
    };
    struct Frame {
        std::string definition;
        std::string current;
        std::map<std::string, int> hits;
        std::map<std::string, std::chrono::steady_clock::time_point> phase_deadlines;
        std::chrono::steady_clock::time_point invoked_at;
        std::chrono::steady_clock::time_point entered_at;
        bool next_pending{};
        bool error_pending{};
        std::optional<contracts::FrameEnvelope> selected_frame;
        std::optional<contracts::Observation> selected_observation;
        std::optional<workflow::EventRule> event;
        std::optional<PendingInput> pending;
        std::vector<EventExit> event_exits;
        std::optional<std::chrono::steady_clock::time_point> exit_pause_started;
        std::optional<std::chrono::steady_clock::time_point> ambiguity_since;
        int ambiguity_priority{};
        workflow::EventClass ambiguity_category{workflow::EventClass::Overlay};
    };
    const workflow::FlowProgram &program_;
    FlowPorts &ports_;
    const std::chrono::steady_clock::time_point deadline_;
    std::vector<Frame> stack_;
    TickResult terminal_{TickState::Progress};

    const workflow::Step &step() const;
    TickResult progress() const;
    TickResult waiting(std::chrono::milliseconds delay) const;
    TickResult fail(std::string code);
    TickResult blocked(std::string code);
    TickResult route_error(Frame &frame, const workflow::Step &current,
                           std::string code);
    TickResult select_next(Frame &frame, const workflow::Step &current);
    TickResult execute_step(Frame &frame, const workflow::Step &current);
    std::optional<TickResult> check_events(Frame &frame, const workflow::Step &current,
                                            const contracts::FrameEnvelope &image,
                                            workflow::EventClass category);
    std::vector<workflow::EventRule> effective_events(const workflow::Step &current) const;
    void advance(Frame &frame);
    contracts::Command command(const workflow::Input &input,
                                const contracts::Observation &target) const;
};
} // namespace wvd::runtime
