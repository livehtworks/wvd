#pragma once

#include "devices/native_input_gate.hpp"
#include "contracts/business_state.hpp"
#include "recognition/service.hpp"
#include "runtime/flow_executor.hpp"
#include <stop_token>

namespace wvd::runtime {
class NativeFlowPorts final : public FlowPorts {
  public:
    using OperationHandler = std::function<OperationResult(
        const std::string &, const nlohmann::json &,
        const std::optional<contracts::FrameEnvelope> &,
        const std::optional<contracts::Observation> &, const std::string &)>;
    using InputSink = std::function<void(const nlohmann::json &)>;
    using CaptureSink = std::function<void(const contracts::FrameEnvelope &)>;
    NativeFlowPorts(devices::DeviceBackend &backend, recognition::Service &recognizer,
                    contracts::BusinessRunState &business, contracts::InputPolicy policy,
                    std::uint64_t generation, std::stop_token stop);
    void set_operation_handler(OperationHandler handler);
    void set_input_sink(InputSink sink) { input_sink_ = std::move(sink); }
    void set_capture_sink(CaptureSink sink) { capture_sink_ = std::move(sink); }
    void input_result(const nlohmann::json &value) noexcept override;
    contracts::FrameEnvelope capture() override;
    bool prepare_input() override { return gate_.prepare_for_input(); }
    void observation_window(std::chrono::steady_clock::time_point deadline) override { gate_.observation_window(deadline); }
    contracts::ObservationRecovery recover_observation(bool restart_application = false) override {
        return backend_.recover_observation(restart_application);
    }
    bool settle_observed_input() override { return backend_.settle_observed_input(); }
    const std::optional<contracts::DiagnosticPixels> &failed_pixels() const { return failed_pixels_; }
    // 只在 Session 工作线程、run() 返回后由协调器读取；不是实时共享可写帧。
    const std::optional<contracts::FrameEnvelope> &last_valid_frame() const { return last_valid_frame_; }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &request) override;
    Submission submit(const contracts::Command &command,
                      const contracts::Observation &scene,
                      const contracts::Observation &target,
                      contracts::Box allowed_area,
                      const std::string &source_path) override;
    OperationResult operate(const std::string &binding,
                            const nlohmann::json &parameters,
                            const std::optional<contracts::FrameEnvelope> &frame,
                            const std::optional<contracts::Observation> &observation,
                            const std::string &source_path) override;
    void scene_observed(const contracts::Observation &observation) override {
        recognizer_.note_known_scene(observation);
    }
    bool cancelled() const override;
    bool reusable(const contracts::FrameIdentity &identity) const override { return gate_.reusable(identity); }
    void stop();
    bool cleanup();
    contracts::InputCounts input_counts() const;
    contracts::FrameIdentity current_identity() const;
    nlohmann::json business_summary() const { return business_.summary(); }
    std::uint64_t business_version() const { return business_.version(); }

  private:
    devices::DeviceBackend &backend_;
    recognition::Service &recognizer_;
    contracts::BusinessRunState &business_;
    const std::string application_id_;
    const bool read_only_viewport_;
    devices::NativeInputGate gate_;
    const std::stop_token stop_token_;
    OperationHandler operation_handler_;
    InputSink input_sink_;
    CaptureSink capture_sink_;
    std::uint64_t input_sequence_{};
    std::optional<contracts::FrameEnvelope> last_valid_frame_;
    std::optional<contracts::DiagnosticPixels> failed_pixels_;
};
} // namespace wvd::runtime
