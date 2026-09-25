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
    contracts::FrameEnvelope capture() override;
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
    bool cancelled() const override;
    void stop();
    bool cleanup();
    contracts::InputCounts input_counts() const;
    contracts::FrameIdentity current_identity() const;

  private:
    recognition::Service &recognizer_;
    contracts::BusinessRunState &business_;
    devices::NativeInputGate gate_;
    const std::stop_token stop_token_;
    OperationHandler operation_handler_;
    InputSink input_sink_;
    CaptureSink capture_sink_;
    std::uint64_t input_sequence_{};
};
} // namespace wvd::runtime
