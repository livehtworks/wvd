#include "native_flow_ports.hpp"

namespace wvd::runtime {
NativeFlowPorts::NativeFlowPorts(devices::DeviceBackend &backend,
    recognition::Service &recognizer, contracts::BusinessRunState &business,
    contracts::InputPolicy policy, std::uint64_t generation,
    std::stop_token stop)
    : recognizer_(recognizer), business_(business),
      gate_(backend, std::move(policy), generation), stop_token_(stop) {}

void NativeFlowPorts::set_operation_handler(OperationHandler handler) {
    if (!handler || operation_handler_)
        throw std::runtime_error("NATIVE_OPERATION_HANDLER_INVALID");
    operation_handler_ = std::move(handler);
}

contracts::FrameEnvelope NativeFlowPorts::capture() {
    if (cancelled()) throw std::runtime_error("CAPTURE_CANCELLED");
    auto frame = gate_.capture();
    if (capture_sink_) capture_sink_(frame);
    return frame;
}

contracts::Observation NativeFlowPorts::recognize(
    const contracts::FrameEnvelope &frame, const recognition::Request &request) {
    if (cancelled()) throw std::runtime_error("RECOGNITION_CANCELLED");
    return recognizer_.evaluate(frame, gate_.current_identity(), request, &business_);
}

Submission NativeFlowPorts::submit(const contracts::Command &command,
    const contracts::Observation &scene, const contracts::Observation &target,
    contracts::Box allowed_area, const std::string &source_path) {
    const auto sequence = ++input_sequence_;
    const auto record = [&](const char *state, std::uint64_t action_epoch,
                            const std::string &detail) {
        if (!input_sink_) return;
        try {
            input_sink_({{"sequence", sequence}, {"source_path", source_path},
                         {"command_kind", static_cast<int>(command.kind)},
                         {"state", state}, {"basis_frame", scene.basis.frame_id},
                         {"basis_epoch", scene.basis.action_epoch},
                         {"action_epoch", action_epoch}, {"detail", detail}});
        } catch (...) { /* Diagnostics cannot alter or repeat an input. */ }
    };
    record("attempted", 0, "");
    devices::InputReceipt receipt;
    try {
        receipt = gate_.submit(command, scene, target, allowed_area);
    } catch (const std::exception &error) {
        record("error", 0, error.what());
        throw;
    } catch (...) {
        record("error", 0, "unknown");
        throw;
    }
    SubmissionState state = SubmissionState::Rejected;
    if (receipt.disposition == devices::InputDisposition::Submitted)
        state = SubmissionState::Accepted;
    else if (receipt.disposition == devices::InputDisposition::Unresolved)
        state = SubmissionState::Unresolved;
    record(state == SubmissionState::Accepted ? "accepted" :
           state == SubmissionState::Unresolved ? "unresolved" : "rejected",
           receipt.action_epoch, receipt.detail);
    return {state, receipt.action_epoch, receipt.submitted_at, receipt.detail};
}

OperationResult NativeFlowPorts::operate(const std::string &binding,
    const nlohmann::json &parameters,
    const std::optional<contracts::FrameEnvelope> &frame,
    const std::optional<contracts::Observation> &observation,
    const std::string &source_path) {
    if (!operation_handler_)
        return {OperationState::Failed, "NATIVE_OPERATION_HANDLER_MISSING"};
    return operation_handler_(binding, parameters, frame, observation, source_path);
}

bool NativeFlowPorts::cancelled() const {
    return stop_token_.stop_requested() || gate_.stopped();
}

void NativeFlowPorts::stop() {
    gate_.stop();
    recognizer_.cancel();
}

bool NativeFlowPorts::cleanup() { return gate_.cleanup(); }
contracts::InputCounts NativeFlowPorts::input_counts() const { return gate_.counts(); }
contracts::FrameIdentity NativeFlowPorts::current_identity() const {
    return gate_.current_identity();
}
} // namespace wvd::runtime
