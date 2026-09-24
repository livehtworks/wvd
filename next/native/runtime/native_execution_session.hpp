#pragma once

#include "native_flow_ports.hpp"
#include <condition_variable>
#include <mutex>
#include <memory>

namespace wvd::runtime {
struct NativeExecutionResult {
    TickResult flow;
    contracts::InputCounts inputs;
    bool inputs_released{};
    bool unresolved_input{};
    nlohmann::json unresolved_inputs = nlohmann::json::array();
    std::string cleanup_error;
};

// The coordinator supplies exactly one worker thread. This session never starts
// another runner and never substitutes a device after an uncertain input.
class NativeExecutionSession final {
  public:
    using OperationFactory = std::function<NativeFlowPorts::OperationHandler(NativeFlowPorts &)>;
    using ProgressSink = std::function<void(const nlohmann::json &)>;
    NativeExecutionSession(const workflow::FlowProgram &program,
        devices::DeviceBackend &backend, std::shared_ptr<recognition::Service> recognizer,
        contracts::BusinessRunState &business, contracts::InputPolicy policy,
        std::uint64_t generation, std::chrono::milliseconds total_budget,
        OperationFactory operations, ProgressSink progress = {},
        NativeFlowPorts::InputSink input_sink = {});
    NativeExecutionResult run();
    void request_stop();

  private:
    std::stop_source stop_source_;
    // 成员顺序保证 ports_ 和 executor_ 先析构，识别服务最后释放。
    std::shared_ptr<recognition::Service> recognizer_owner_;
    NativeFlowPorts ports_;
    FlowExecutor executor_;
    ProgressSink progress_;
    mutable std::mutex wait_mutex_;
    std::condition_variable_any wake_;
};
} // namespace wvd::runtime
