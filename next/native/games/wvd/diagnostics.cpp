#include "diagnostics.hpp"
#include "state.hpp"

namespace wvd::games {
namespace {
using J = nlohmann::json;
bool confirm(maafw::Context &context, const J &parameters, const J &) {
    if (context.cancelled())
        return false;
    const auto event = parameters.at("event").get<std::string>();
    const auto operation = parameters.at("operation").get<std::string>();
    const auto frame = context.capture();
    auto request = maafw::parse_recognition_request(parameters.at("confirmation"));
    auto observed = context.recognize(frame, request);
    if (observed.outcome == contracts::RecognitionOutcome::Error)
        throw std::runtime_error(observed.error_code);
    if (observed.outcome != contracts::RecognitionOutcome::Hit || !observed.action_eligible)
        throw std::runtime_error("BUSINESS_CONFIRMATION_MISSING");
    if (observed.basis.frame_id != frame.identity.frame_id ||
        observed.basis.generation != frame.identity.generation)
        throw std::runtime_error("BUSINESS_CONFIRMATION_FRAME_INVALID");
    std::optional<std::size_t> step;
    std::optional<std::size_t> reward_index;
    if (event == "mining_reward_observed") {
        if (parameters.at("confirmation").at("parameters").value("mode", "") != "mining_reward")
            throw std::runtime_error("MINING_REWARD_RECOGNITION_REQUIRED");
        reward_index = observed.evidence.at("evidence").at("selected_index").get<std::size_t>();
    }
    if (parameters.contains("expected_step")) {
        const auto &value = parameters.at("expected_step");
        if (!value.is_number_integer() || value < 0 || value > 4096)
            throw std::runtime_error("BUSINESS_TASK_STEP_INVALID");
        step = value.get<std::size_t>();
    }
    J receipt;
    const bool accepted = context.with_business_state([&](contracts::BusinessRunState &base) {
        if (context.cancelled())
            return false;
        if (!context.current_observation(observed))
            throw std::runtime_error("BUSINESS_CONFIRMATION_STALE");
        auto &state = dynamic_cast<WvdRunState &>(base);
        // 普通段和恢复段共享 Run 身份；代次不进入幂等 ID，否则恢复后会重复提交。
        const auto id = state.confirmation_id(operation, event);
        const auto applied = state.confirm_event(id, event, observed.basis.generation,
                                                  observed.basis.frame_id, step, reward_index);
        receipt = {{"operation_id", id}, {"event", event}, {"applied", applied},
                    {"frame_id", observed.basis.frame_id}, {"generation", observed.basis.generation}};
        return true;
    });
    if (accepted)
        context.business_event("confirmed", receipt);
    return accepted;
}
}
void register_wvd_confirmations(runtime::BehaviorRegistry &registry) {
    registry.add_action({"wvd.confirmation", "1"}, confirm);
}
contracts::BehaviorBinding wvd_confirmation_binding() {
    return {"WvdConfirm", {"wvd.confirmation", "1"}, nlohmann::json::object()};
}
}
