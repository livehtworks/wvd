#include "native_execution_session.hpp"
#include "platform/execution_timing.hpp"
#include "platform/json_storage.hpp"
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
    NativeFlowPorts::CaptureSink capture_sink, ProgressSink timing_sink)
    : program_owner_(std::move(program)), recognizer_owner_(std::move(recognizer)),
      ports_(backend, required_service(recognizer_owner_), business, std::move(policy), generation,
             stop_source_.get_token()),
      executor_(required_program(program_owner_), ports_, total_budget), progress_(std::move(progress)),
      timing_sink_(std::move(timing_sink)) {
    if (!operations) throw std::runtime_error("NATIVE_OPERATION_FACTORY_MISSING");
    ports_.set_operation_handler(operations(ports_));
    ports_.set_input_sink(std::move(input_sink));
    ports_.set_capture_sink(std::move(capture_sink));
    lifetime_.ready();
}

nlohmann::json NativeExecutionSession::ownership_snapshot() const {
    std::size_t steps{};
    platform::JsonStorageEstimate parameters;
    const auto count_request = [&](const recognition::Request &request) {
        if (const auto *custom = std::get_if<recognition::CustomParameters>(&request.parameters)) parameters.add(custom->parameters);
    };
    for (const auto &[id, definition] : program_owner_->definitions) {
        (void)id;
        steps += definition.steps.size();
        for (const auto &[name, step] : definition.steps) {
            (void)name;
            if (step.guard) count_request(*step.guard);
            if (step.business_guard) parameters.add(*step.business_guard);
            std::visit([&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, workflow::Observe>) count_request(value.request);
                else if constexpr (std::is_same_v<T, workflow::Input>) {
                    count_request(value.scene); count_request(value.target); parameters.add(value.command);
                    if (value.retry) count_request(value.retry->ready);
                } else if constexpr (std::is_same_v<T, workflow::AwaitResult>) count_request(value.condition);
                else if constexpr (std::is_same_v<T, workflow::BusinessConfirm>) {
                    count_request(value.condition); parameters.add(value.parameters);
                } else if constexpr (std::is_same_v<T, workflow::RegisteredOperation>) parameters.add(value.parameters);
                else if constexpr (std::is_same_v<T, workflow::Poll>) {
                    if (value.ongoing) count_request(*value.ongoing);
                    if (value.progress) count_request(*value.progress);
                }
            }, step.data);
        }
    }
    return {{"owner_id", lifetime_.id()}, {"program_revision", program_owner_->revision},
        {"program_owners", program_owner_.use_count()}, {"recognizer_owners", recognizer_owner_.use_count()},
        {"program_definitions", program_owner_->definitions.size()}, {"program_steps", steps},
        {"program_parameter_containers", parameters.json()},
        {"excluded", {"allocator_overhead", "event_request_containers", "callback_captures", "external_runtime_allocations"}}};
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
    nlohmann::json objective;
    std::optional<std::uint64_t> business_version;
    auto segment_at = started;
    auto segment_sample = totals.sample();
    auto segment_node = executor_.current_step_id();
    auto segment_source = executor_.current_source_path();
    auto segment_depth = executor_.invocation_depth();
    std::uint64_t segment_sequence{}, segment_ticks{}, timing_errors{};
    // 同节点轮询合为一段；事件/子流程切换即结段，避免逐帧日志放大。
    // segment_end 只说明离开这一段，不等于动作成功，成功由 input.result 正向证据记录。
    const auto finish_segment = [&](const char *ending) {
        if (!segment_ticks || !timing_sink_) return;
        try {
            const auto now = std::chrono::steady_clock::now();
            auto data = timing::report(totals.sample(),
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - segment_at).count(), segment_sample);
            data["sequence"] = ++segment_sequence;
            data["node_id"] = segment_node; data["source_path"] = segment_source;
            data["depth"] = segment_depth; data["ticks"] = segment_ticks;
            data["session_offset_ns"] = std::chrono::duration_cast<std::chrono::nanoseconds>(segment_at - started).count();
            data["ending"] = ending; data["reason"] = result.flow.code;
            timing::Scope measure(timing::Part::JsonEvents);
            timing_sink_(data);
        } catch (...) { ++timing_errors; }
    };
    try {
        for (;;) {
            const auto node = executor_.current_step_id();
            const auto source = executor_.current_source_path();
            const auto depth = executor_.invocation_depth();
            if (node != segment_node || source != segment_source || depth != segment_depth) {
                finish_segment("segment_end");
                segment_node = node; segment_source = source; segment_depth = depth;
                segment_at = std::chrono::steady_clock::now(); segment_sample = totals.sample(); segment_ticks = 0;
            }
            ++segment_ticks;
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
                const auto version = ports_.business_version();
                if (business_version != version) {
                    const auto summary = ports_.business_summary();
                    objective = nlohmann::json::object();
                    for (const auto *key : {"farm_target_text", "task_step", "bounty_cycle", "healing_required"})
                        if (summary.contains(key)) objective[key] = summary.at(key);
                    business_version = version;
                }
                current["main_objective"] = objective;
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
    constexpr const char *endings[]{"progress", "waiting", "completed", "business_failed", "failed", "external_blocked", "cancelled"};
    finish_segment(endings[static_cast<unsigned>(result.flow.state)]);
    executor_.report_unconfirmed_inputs(result.flow.code);
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
        result.observation_recovery = executor_.progress_snapshot().value("observation_recovery", nlohmann::json(nullptr));
        result.unresolved_inputs = executor_.progress_snapshot().value(
            "pending_inputs", nlohmann::json::array());
        result.performance = timing::report(totals.sample(), std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
        result.performance["skills"] = std::move(skills);
        result.performance["timing_segments"] = segment_sequence;
        result.performance["timing_errors"] = timing_errors;
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
