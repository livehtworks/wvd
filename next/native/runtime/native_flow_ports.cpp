#include "native_flow_ports.hpp"
#include "platform/execution_timing.hpp"

namespace wvd::runtime {
NativeFlowPorts::NativeFlowPorts(devices::DeviceBackend &backend,
    recognition::Service &recognizer, contracts::BusinessRunState &business,
    contracts::InputPolicy policy, std::uint64_t generation,
    std::stop_token stop)
    : backend_(backend), recognizer_(recognizer), business_(business),
      application_id_(policy.application_id), read_only_viewport_(policy.observed_read_only_viewport),
      gate_(backend, std::move(policy), generation), stop_token_(stop) {}

void NativeFlowPorts::set_operation_handler(OperationHandler handler) {
    if (!handler || operation_handler_)
        throw std::runtime_error("NATIVE_OPERATION_HANDLER_INVALID");
    operation_handler_ = std::move(handler);
}

contracts::FrameEnvelope NativeFlowPorts::capture() {
    if (cancelled()) throw std::runtime_error("CAPTURE_CANCELLED");
    contracts::FrameEnvelope frame;
    try { frame = gate_.capture(); }
    catch (...) {
        failed_pixels_ = backend_.failed_pixels();
        throw;
    }
    // 桌面/其它APP保留真实像素供诊断，但绝不送进游戏ROI，更不授权点击。
    // 只读设备预览仍可看桌面；游戏会话交由原有生命周期所有者恢复。
    if (!read_only_viewport_ && frame.identity.foreground_application != application_id_) {
        if (frame.raw_bgr)
            failed_pixels_ = contracts::DiagnosticPixels{frame.identity.raw_size, frame.raw_bgr,
                frame.identity.captured_at, frame.identity.device_id, frame.identity.backend};
        if (capture_sink_) capture_sink_(frame); // 诊断消费者区分非游戏像素，不能做游戏识图。
        throw contracts::ObservationUnavailable({contracts::ReadFaultKind::ApplicationUnavailable,
            contracts::ReadFaultStage::Capture, "GAME_NOT_FOREGROUND", "capture.foreground", {}, {},
            {{"foreground_application", frame.identity.foreground_application},
             {"expected_application", application_id_},
             {"size", {frame.identity.raw_size.width, frame.identity.raw_size.height}}}});
    }
    // 原生 BGR 共享像素所有权；非游戏帧不会替换最后有效游戏帧。
    failed_pixels_.reset();
    last_valid_frame_ = frame;
    platform::timing::count(platform::timing::Counter::Captures);
    if (capture_sink_) {
        platform::timing::Scope measure(platform::timing::Part::ArchiveSubmit);
        capture_sink_(frame);
    }
    return frame;
}

contracts::Observation NativeFlowPorts::recognize(
    const contracts::FrameEnvelope &frame, const recognition::Request &request) {
    if (cancelled()) throw std::runtime_error("RECOGNITION_CANCELLED");
    platform::timing::Scope measure(platform::timing::Part::Recognition);
    return recognizer_.evaluate(frame, gate_.current_identity(), request, &business_);
}

Submission NativeFlowPorts::submit(const contracts::Command &command,
    const contracts::Observation &scene, const contracts::Observation &target,
    contracts::Box allowed_area, const std::string &source_path) {
    const auto sequence = ++input_sequence_;
    const auto started = std::chrono::steady_clock::now();
    const auto before = platform::timing::active ? platform::timing::active->sample() : platform::timing::Sample{};
    const auto record = [&](const char *state, std::uint64_t action_epoch,
                            const std::string &detail) {
        if (!input_sink_) return;
        platform::timing::Scope measure(platform::timing::Part::JsonEvents);
        try {
            auto event = nlohmann::json{{"sequence", sequence}, {"source_path", source_path},
                         {"command_kind", static_cast<int>(command.kind)},
                         {"position", {command.x, command.y}},
                         {"end_position", {command.x2, command.y2}}, {"key", command.key},
                         {"target_center", target.center ? nlohmann::json::array({target.center->x, target.center->y}) : nlohmann::json(nullptr)},
                         {"state", state}, {"basis_frame", scene.basis.frame_id},
                         {"basis_epoch", scene.basis.action_epoch},
                         {"action_epoch", action_epoch}, {"detail", detail}};
            if (std::string_view(state) != "attempted")
                event["timing"] = platform::timing::report(
                    platform::timing::active ? platform::timing::active->sample() : before,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count(), before);
            input_sink_(event);
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
    return {state, receipt.action_epoch, receipt.submitted_at, receipt.detail, receipt.read_fault};
}

void NativeFlowPorts::input_result(const nlohmann::json &value) noexcept {
    if (!input_sink_) return;
    try {
        platform::timing::Scope measure(platform::timing::Part::JsonEvents);
        auto event = value;
        event["state"] = "result";
        input_sink_(event);
    } catch (...) {}
}

OperationResult NativeFlowPorts::operate(const std::string &binding,
    const nlohmann::json &parameters,
    const std::optional<contracts::FrameEnvelope> &frame,
    const std::optional<contracts::Observation> &observation,
    const std::string &source_path) {
    if (!operation_handler_)
        return {OperationState::Failed, "NATIVE_OPERATION_HANDLER_MISSING"};
    if (binding == "BusinessPredicate") platform::timing::count(platform::timing::Counter::BusinessPredicates);
    return operation_handler_(binding, parameters, frame, observation, source_path);
}

bool NativeFlowPorts::cancelled() const {
    return stop_token_.stop_requested() || gate_.stopped();
}

void NativeFlowPorts::stop() {
    gate_.stop();
    recognizer_.cancel();
}

bool NativeFlowPorts::cleanup() {
    // 只由工作线程收尾，不能由stop线程改动设备读取窗口。
    backend_.observation_window({}, {});
    return gate_.cleanup();
}
contracts::InputCounts NativeFlowPorts::input_counts() const { return gate_.counts(); }
contracts::FrameIdentity NativeFlowPorts::current_identity() const {
    return gate_.current_identity();
}
} // namespace wvd::runtime
