#include "games/wvd/combat/turn.hpp"
#include "games/wvd/state.hpp"
#include "legacy_event_bindings.hpp"
#include "runtime/behavior_registry.hpp"

namespace wvd::games::combat {
using J = nlohmann::json;
namespace {
J request(J parameters) {
    return {{"id", "combat.turn"}, {"revision", "1"}, {"type", "custom"},
            {"binding", "WvdVision"}, {"roi", {0, 0, 900, 1600}},
            {"parameters", std::move(parameters)}};
}
bool combat_action(maafw::Context &context, const J &p, const J &) {
    if (context.cancelled()) return false;
    const auto confirmed = recovery::confirm_with_events(
        context, p.at("confirmation"), "COMBAT_CONFIRMATION_MISSING");
    if (!confirmed) return context.event_replan_pending(context.node());
    const auto &frame = confirmed->frame;
    const auto &confirmation = confirmed->observation;
    const auto operation = p.at("operation").get<std::string>();
    std::vector<PortraitScore> scores;
    if (operation == "prepare") {
        for (const auto &candidate : p.at("portraits")) {
            if (context.cancelled()) return false;
            auto observed = context.recognize(frame, maafw::parse_recognition_request(
                request({{"mode", "portrait"}, {"image", candidate.at("image")}})));
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                throw std::runtime_error(observed.error_code);
            scores.push_back({candidate.at("role"),
                observed.evidence.at("evidence").at("best_score")});
        }
    } else if (operation != "success" && operation != "auto_confirmed") {
        throw std::runtime_error("COMBAT_OPERATION_UNKNOWN");
    }
    J receipt;
    const bool accepted = context.with_business_state([&](contracts::BusinessRunState &base) {
        if (context.cancelled()) return false;
        if (!context.current_observation(confirmation))
            throw std::runtime_error("COMBAT_CONFIRMATION_STALE");
        auto &state = dynamic_cast<WvdRunState &>(base);
        if (operation == "prepare") state.prepare_skill(scores, p.at("catalog"));
        else state.finish_prepared_skill(p.at("index"),
            operation == "success" ? SkillOutcome::Succeeded :
                SkillOutcome::AutoFallbackConfirmed);
        receipt = {{"operation", operation}, {"frame_id", frame.identity.frame_id},
                   {"generation", frame.identity.generation},
                   {"consumed", operation != "prepare"}};
        return true;
    });
    if (accepted) context.business_event("combat", receipt);
    return accepted;
}
} // namespace
void register_combat(runtime::BehaviorRegistry &registry) {
    registry.add_action({"wvd.combat", "1"}, combat_action);
}
contracts::BehaviorBinding combat_binding() {
    return {"WvdCombat", {"wvd.combat", "1"}, J::object()};
}
} // namespace wvd::games::combat
