#pragma once

#include "native_flow_ports.hpp"
#include <condition_variable>
#include <mutex>

namespace wvd::runtime {
struct NativeExecutionResult {
    TickResult flow;
    contracts::InputCounts inputs;
    bool inputs_released{};
    bool unresolved_input{};
    std::string cleanup_error;
};

// The coordinator supplies exactly one worker thread. This session never starts
// another runner and never substitutes a device after an uncertain input.
class NativeExecutionSession final {
  public:
    using OperationFactory = std::function<NativeFlowPorts::OperationHandler(NativeFlowPorts &)>;
    using ProgressSink = std::function<void(const std::string &, const std::string &)>;
    NativeExecutionSession(const workflow::FlowProgram &program,
        devices::DeviceBackend &backend, recognition::Service &recognizer,
        contracts::BusinessRunState &business, contracts::InputPolicy policy,
        std::uint64_t generation, std::chrono::milliseconds total_budget,
        OperationFactory operations, ProgressSink progress = {});
    NativeExecutionResult run();
    void request_stop();

  private:
    std::stop_source stop_source_;
    NativeFlowPorts ports_;
    FlowExecutor executor_;
    ProgressSink progress_;
    mutable std::mutex wait_mutex_;
    std::condition_variable_any wake_;
};
} // namespace wvd::runtime
