#pragma once
#include "maafw/gateway.hpp"
#include <optional>
#include <thread>

namespace wvd::games::recovery {
struct ConfirmedFrame {
    contracts::FrameEnvelope frame;
    contracts::Observation observation;
};

inline std::optional<ConfirmedFrame> confirm_with_events(
    maafw::Context &context, const nlohmann::json &request, const char *missing) {
    using contracts::FlowEventOutcome;
    using contracts::FlowEventPhase;
    using contracts::RecognitionOutcome;
    while (!context.cancelled()) {
        auto frame = context.capture();
        const auto overlay = context.check_events(frame, FlowEventPhase::Overlay);
        if (overlay.outcome == FlowEventOutcome::ExternalBlocked ||
            overlay.outcome == FlowEventOutcome::Cancelled) return std::nullopt;
        if (overlay.outcome == FlowEventOutcome::Replan) return std::nullopt;
        if (overlay.outcome == FlowEventOutcome::Handled || overlay.outcome == FlowEventOutcome::Reobserve) {
            if (overlay.outcome == FlowEventOutcome::Reobserve)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        auto observation = context.recognize(frame, maafw::parse_recognition_request(request));
        if (observation.outcome == RecognitionOutcome::Error)
            throw std::runtime_error(observation.error_code);
        if (observation.outcome == RecognitionOutcome::Hit && observation.action_eligible)
            return ConfirmedFrame{std::move(frame), std::move(observation)};
        const auto encounter = context.check_events(frame, FlowEventPhase::Encounter);
        if (encounter.outcome == FlowEventOutcome::ExternalBlocked ||
            encounter.outcome == FlowEventOutcome::Cancelled) return std::nullopt;
        if (encounter.outcome == FlowEventOutcome::Replan) return std::nullopt;
        if (encounter.outcome == FlowEventOutcome::Handled || encounter.outcome == FlowEventOutcome::Reobserve) {
            if (encounter.outcome == FlowEventOutcome::Reobserve)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        throw std::runtime_error(missing);
    }
    return std::nullopt;
}
} // namespace wvd::games::recovery
