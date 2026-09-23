#pragma once
#include <chrono>
#include <string>

namespace wvd::contracts {
enum class FlowEventOutcome { NoEvent, Handled, Reobserve, Replan, ExternalBlocked, Cancelled, Error };
enum class FlowEventPhase { Overlay, Encounter };
struct FlowEventResult {
    FlowEventOutcome outcome{FlowEventOutcome::NoEvent};
    std::string event_id;
    std::string resume_node;
    std::chrono::steady_clock::duration elapsed{};
};
} // namespace wvd::contracts
