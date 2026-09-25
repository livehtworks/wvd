#include "native_execution_session.hpp"
#include <algorithm>
#include <stdexcept>

namespace wvd::runtime {
namespace {
recognition::Service &required_service(const std::shared_ptr<recognition::Service> &owner) {
    if (!owner) throw std::runtime_error("NATIVE_RECOGNIZER_OWNER_MISSING");
    return *owner;
}
} // namespace
NativeExecutionSession::NativeExecutionSession(const workflow::FlowProgram &program,
    devices::DeviceBackend &backend, std::shared_ptr<recognition::Service> recognizer,
    contracts::BusinessRunState &business, contracts::InputPolicy policy,
    std::uint64_t generation, std::chrono::milliseconds total_budget,
    OperationFactory operations, ProgressSink progress, NativeFlowPorts::InputSink input_sink,
    NativeFlowPorts::CaptureSink capture_sink)
    : recognizer_owner_(std::move(recognizer)),
      ports_(backend, required_service(recognizer_owner_), business, std::move(policy), generation,
             stop_source_.get_token()),
      executor_(program, ports_, total_budget), progress_(std::move(progress)) {
    if (!operations) throw std::runtime_error("NATIVE_OPERATION_FACTORY_MISSING");
    ports_.set_operation_handler(operations(ports_));
    ports_.set_input_sink(std::move(input_sink));
    ports_.set_capture_sink(std::move(capture_sink));
}

NativeExecutionResult NativeExecutionSession::run() {
    NativeExecutionResult result;
    nlohmann::json reported_progress;
    try {
        for (;;) {
            result.flow = executor_.tick();
            if (progress_) {
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
    result.unresolved_input = executor_.has_unresolved_input();
    result.unresolved_inputs = executor_.progress_snapshot().value("pending_inputs", nlohmann::json::array());
    if (stop_source_.stop_requested())
        result.flow = {TickState::Cancelled, {}, "STOP_REQUESTED",
                       executor_.current_source_path()};
    ports_.stop();
    try {
        result.inputs_released = ports_.cleanup();
        if (!result.inputs_released) result.cleanup_error = "INPUT_RELEASE_UNCONFIRMED";
    } catch (const std::exception &error) {
        result.cleanup_error = error.what();
    } catch (...) {
        result.cleanup_error = "NATIVE_CLEANUP_EXCEPTION";
    }
    result.inputs = ports_.input_counts();
    return result;
}

void NativeExecutionSession::request_stop() {
    stop_source_.request_stop();
    ports_.stop();
    wake_.notify_all();
}

} // namespace wvd::runtime
