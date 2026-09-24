#include "games/wvd/chest/chest.hpp"
#include "runtime/behavior_registry.hpp"
#include "games/wvd/state.hpp"
#include "legacy_event_bindings.hpp"

namespace wvd::games::chest {
namespace {
using J = nlohmann::json;
bool prepare_character(maafw::Context &context, const J &parameters, const J &) {
    if (context.cancelled())
        return false;
    const auto confirmed = recovery::confirm_with_events(
        context, parameters.at("confirmation"), "CHEST_SELECTION_SCENE_MISSING");
    if (!confirmed) return context.event_replan_pending(context.node());
    const auto &frame = confirmed->frame;
    const auto &scene = confirmed->observation;
    std::array<bool, 6> fear{};
    for (unsigned i = 0; i < fear.size(); ++i) {
        if (context.cancelled())
            return false;
        const int x = 258 + (i % 3) * 258, y = 1161 + (i / 3) * 184;
        const J request{{"id", "chest.fear." + std::to_string(i)}, {"revision", "1"},
            {"type", "custom"}, {"binding", "WvdVision"}, {"roi", {0, 0, 900, 1600}},
            {"parameters", {{"mode", "template"}, {"image", parameters.at("image")},
                            {"roi", {x - 125, y - 82, 250, 164}}}}};
        const auto observed = context.recognize(frame, maafw::parse_recognition_request(request));
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            throw std::runtime_error(observed.error_code);
        fear[i] = observed.outcome == contracts::RecognitionOutcome::Hit;
    }
    J receipt;
    const auto accepted = context.with_business_state([&](contracts::BusinessRunState &base) {
        if (context.cancelled())
            return false;
        if (!context.current_observation(scene))
            throw std::runtime_error("CHEST_CONFIRMATION_STALE");
        auto &state = dynamic_cast<WvdRunState &>(base);
        state.prepare_chest_character(fear, parameters.at("preferred").get<int>(), parameters.at("seed").get<std::uint32_t>());
        const auto summary = state.summary();
        receipt = {{"selected", summary.at("chest_character")}, {"available_mask", summary.at("chest_available_mask")},
            {"frame_id", frame.identity.frame_id}, {"generation", frame.identity.generation}};
        return true;
    });
    if (accepted)
        context.business_event("chest_selection", receipt);
    return accepted;
}
}
void register_chest(runtime::BehaviorRegistry &registry) {
    registry.add_action({"wvd.chest_selection", "1"}, prepare_character);
}
contracts::BehaviorBinding chest_binding() {
    return {"WvdChest", {"wvd.chest_selection", "1"}, nlohmann::json::object()};
}
}
