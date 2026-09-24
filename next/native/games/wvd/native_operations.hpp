#pragma once

#include "contracts/recognition.hpp"
#include "runtime/native_execution_session.hpp"
#include <functional>
#include <optional>

namespace wvd::games {
class WvdRunState;

// The runtime owns scheduling and input. These callbacks only borrow the current
// observation capability; WVD operations never get a device controller.
struct OperationContext {
    std::function<contracts::FrameEnvelope()> capture;
    std::function<contracts::Observation(const contracts::FrameEnvelope &,
                                         const recognition::Request &)> recognize;
    std::function<contracts::FrameIdentity()> current_identity;
    std::function<bool()> cancelled;
    std::function<void(const std::string &, const nlohmann::json &)> business_event;
    std::function<void(const std::string &)> checkpoint;
};

class NativeOperations final {
  public:
    NativeOperations(WvdRunState &state, OperationContext context);
    runtime::OperationResult execute(const std::string &binding,
        const nlohmann::json &parameters,
        const std::optional<contracts::FrameEnvelope> &selected_frame,
        const std::optional<contracts::Observation> &selected_observation,
        const std::string &source_path);

  private:
    WvdRunState &state_;
    OperationContext context_;
    contracts::Observation observe(const contracts::FrameEnvelope &frame,
                                   nlohmann::json parameters,
                                   const std::string &id) const;
    bool current(const contracts::Observation &observation) const;
};

runtime::NativeExecutionSession::OperationFactory wvd_operation_factory(
    WvdRunState &state,
    std::function<void(const std::string &, const nlohmann::json &)> business_event,
    std::function<void(const std::string &)> checkpoint);
} // namespace wvd::games
