#include "native_execution_session.hpp"
#include "platform/execution_timing.hpp"
#include <algorithm>
#include <stdexcept>

namespace wvd::runtime {
namespace {
const workflow::FlowProgram &required_program(
    const std::shared_ptr<const workflow::FlowProgram> &owner) {
    if (!owner) throw std::runtime_error("NATIVE_PROGRAM_OWNER_MISSING");
    return *owner;
}
// 在任何结果对象/JSON构造之前就建立清理责任，异常展开不跳过输入关闭与回收。
struct SessionCleanup {
    NativeFlowPorts &ports;
    bool attempted{};
    ~SessionCleanup() noexcept {
        if (attempted) return;
        try { ports.stop(); } catch (...) {}
        try { (void)ports.cleanup(); } catch (...) {}
    }
};
recognition::Service &required_service(const std::shared_ptr<recognition::Service> &owner) {
    if (!owner) throw std::runtime_error("NATIVE_RECOGNIZER_OWNER_MISSING");
    return *owner;
}
} // namespace
NativeExecutionSession::NativeExecutionSession(std::shared_ptr<const workflow::FlowProgram> program,
    devices::DeviceBackend &backend, std::shared_ptr<recognition::Service> recognizer,
    contracts::BusinessRunState &business, contracts::InputPolicy policy,
    std::uint64_t generation, std::chrono::milliseconds total_budget,
    OperationFactory operations, ProgressSink progress, NativeFlowPorts::InputSink input_sink,
    NativeFlowPorts::CaptureSink capture_sink)
    : program_owner_(std::move(program)), recognizer_owner_(std::move(recognizer)),
      ports_(backend, required_service(recognizer_owner_), business, std::move(policy), generation,
             stop_source_.get_token()),
      executor_(required_program(program_owner_), ports_, total_budget), progress_(std::move(progress)) {
    if (!operations) throw std::runtime_error("NATIVE_OPERATION_FACTORY_MISSING");
    ports_.set_operation_handler(operations(ports_));
    ports_.set_input_sink(std::move(input_sink));
    ports_.set_capture_sink(std::move(capture_sink));
}

NativeExecutionResult NativeExecutionSession::run() {
    SessionCleanup cleanup{ports_};
    namespace timing = platform::timing;
    timing::Totals totals;
    timing::Bind metrics(&totals);
    const auto started = std::chrono::steady_clock::now();
    std::optional<timing::Sample> skill_start;
    std::chrono::steady_clock::time_point skill_at{};
    std::string skill_entry;
    nlohmann::json skills = nlohmann::json::array();
    NativeExecutionResult result;
    nlohmann::json reported_progress;
    try {
        for (;;) {
            if (!skill_start && executor_.is_operation("WvdCombat", "prepare")) {
                skill_at = std::chrono::steady_clock::now(); skill_start = totals.sample();
                skill_entry = executor_.current_step_id();
            }
            const auto skill_exit = executor_.current_step_id();
            const bool skill_confirm = executor_.is_operation("WvdCombat", "success") ||
                executor_.is_operation("WvdCombat", "auto_confirmed");
            result.flow = executor_.tick();
            if (skill_start && skill_confirm && result.flow.state == TickState::Progress &&
                executor_.current_step_id() == skill_exit && executor_.operation_advanced()) {
                auto interval = timing::report(totals.sample(), std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - skill_at).count(), *skill_start);
                interval["from"] = skill_entry; interval["to"] = skill_exit; interval["confirmed"] = true;
                if (skills.size() < 32) skills.push_back(std::move(interval));
                skill_start.reset();
            }
            if (progress_) {
                timing::Scope measure(timing::Part::JsonEvents);
                auto current = executor_.progress_snapshot();
                if (current != reported_progress) {
                    reported_progress = current;
                    progress_(current);
                }
            }
            if (result.flow.state == TickState::Completed ||
                result.flow.state == TickState::BusinessFailed ||
                result.flow.state == TickState::Failed ||
                result.flow.state == TickState::Cancelled ||
                result.flow.state == TickState::ExternalBlocked)
                break;
            if (result.flow.state == TickState::Waiting) {
                timing::Scope measure(timing::Part::ExplicitWait);
                std::unique_lock lock(wait_mutex_);
                wake_.wait_until(lock, stop_source_.get_token(),
                    result.flow.wake_at, [] { return false; });
            }
        }
    } catch (const std::exception &error) {
        result.flow = {TickState::Failed, {}, error.what(), executor_.current_source_path()};
    } catch (...) {
        result.flow = {TickState::Failed, {}, "NATIVE_SESSION_EXCEPTION",
                       executor_.current_source_path()};
    }
    // 先保存无需分配的安全事实，并完成回收，再做可以失败的富诊断。
    result.unresolved_input = executor_.has_unresolved_input();
    ports_.stop();
    cleanup.attempted = true;
    try {
        result.inputs_released = ports_.cleanup();
        if (!result.inputs_released) result.cleanup_error = "INPUT_RELEASE_UNCONFIRMED";
    } catch (const std::exception &error) {
        result.cleanup_error = error.what();
    } catch (...) {
        result.cleanup_error = "NATIVE_CLEANUP_EXCEPTION";
    }
    result.inputs = ports_.input_counts();
    if (stop_source_.stop_requested())
        result.flow = {TickState::Cancelled, {}, "STOP_REQUESTED",
                       executor_.current_source_path()};
    try {
        result.unresolved_inputs = executor_.progress_snapshot().value(
            "pending_inputs", nlohmann::json::array());
        result.performance = timing::report(totals.sample(), std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
        result.performance["skills"] = std::move(skills);
    } catch (...) {
        result.details_complete = false;
        // 不清除 unresolved_input，不把空明细当作可以恢复/再次输入的依据。
        if (result.flow.state != TickState::Cancelled)
            result.flow.state = TickState::Failed;
    }
    return result;
}

void NativeExecutionSession::request_stop() {
    stop_source_.request_stop();
    ports_.stop();
    wake_.notify_all();
}

} // namespace wvd::runtime
