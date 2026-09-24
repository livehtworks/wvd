#include "native_operations.hpp"
#include "games/wvd/state.hpp"
#include "recognition/request.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace wvd::games {
namespace {
using J = nlohmann::json;
using Outcome = contracts::RecognitionOutcome;
using Result = runtime::OperationResult;
using State = runtime::OperationState;

Result done() { return {State::Done}; }
Result waiting() { return {State::Waiting, {}, std::chrono::steady_clock::now() +
    std::chrono::milliseconds{50}}; }

void require_valid(const contracts::Observation &observation) {
    if (observation.outcome == Outcome::Error)
        throw std::runtime_error(observation.error_code.empty() ?
            "WVD_RECOGNITION_ERROR" : observation.error_code);
}
bool same_context(const contracts::FrameIdentity &left,
                  const contracts::FrameIdentity &right) {
    return left.device_id == right.device_id &&
        left.game_id == right.game_id &&
        left.pack_revision == right.pack_revision &&
        left.generation == right.generation &&
        left.connection_generation == right.connection_generation &&
        left.action_epoch == right.action_epoch &&
        left.viewport_id == right.viewport_id &&
        left.recognition_size == right.recognition_size;
}
} // namespace

NativeOperations::NativeOperations(WvdRunState &state, OperationContext context)
    : state_(state), context_(std::move(context)) {
    if (!context_.capture || !context_.recognize || !context_.current_identity ||
        !context_.cancelled || !context_.business_event || !context_.checkpoint)
        throw std::runtime_error("WVD_OPERATION_CONTEXT_INVALID");
}

contracts::Observation NativeOperations::observe(const contracts::FrameEnvelope &frame,
                                                 J parameters,
                                                 const std::string &id) const {
    const auto request = recognition::parse_request({{"id", id}, {"revision", "1"},
        {"type", "custom"}, {"binding", "WvdVision"},
        {"roi", {0, 0, frame.identity.recognition_size.width,
                  frame.identity.recognition_size.height}},
        {"parameters", std::move(parameters)}});
    auto observation = context_.recognize(frame, request);
    require_valid(observation);
    if (observation.basis.frame_id != frame.identity.frame_id ||
        !same_context(observation.basis, frame.identity))
        throw std::runtime_error("WVD_OPERATION_OBSERVATION_MISMATCH");
    return observation;
}

bool NativeOperations::current(const contracts::Observation &observation) const {
    return !context_.cancelled() &&
        same_context(observation.basis, context_.current_identity());
}

Result NativeOperations::execute(const std::string &binding, const J &parameters,
    const std::optional<contracts::FrameEnvelope> &selected_frame,
    const std::optional<contracts::Observation> &selected_observation,
    const std::string &source_path) {
    if (context_.cancelled()) return {State::ExternalBlocked, "CANCELLED"};
    if (binding == "BusinessCheckpoint") {
        context_.checkpoint(source_path);
        return done();
    }
    if (binding != "WvdConfirm" && binding != "WvdCombat" &&
        binding != "WvdChest" && binding != "WvdUnknownLeap")
        throw std::runtime_error("WVD_OPERATION_UNREGISTERED:" + binding);
    const auto frame = selected_frame.value_or(context_.capture());
    (void)selected_observation;
    if (binding == "WvdUnknownLeap") {
        J unknown_parameters{{"mode", "unknown_exhausted"}, {"max_tries", 4}};
        if (parameters.contains("extra_known"))
            unknown_parameters["extra_known"] = parameters.at("extra_known");
        const auto unknown = observe(frame, unknown_parameters, "wvd.unknown_leap");
        if (unknown.outcome != Outcome::Hit) return waiting();
        const auto leap = observe(frame,
            {{"mode", "template"}, {"image", "cursedWheel_timeLeap"},
             {"threshold", .8}}, "wvd.unknown_leap.marker");
        if (leap.outcome != Outcome::Hit) return waiting();
        const auto samples = unknown.evidence.at("evidence").at("samples").get<std::uint64_t>();
        J receipt;
        const bool accepted = state_.apply([&](contracts::BusinessRunState &base) {
            if (!current(unknown) || !current(leap)) return false;
            auto &state = dynamic_cast<WvdRunState &>(base);
            if (!state.observe_unknown_leap(samples, frame.identity.generation,
                                            frame.identity.frame_id)) return false;
            receipt = state.summary().at("handoff_intent");
            return true;
        });
        if (!accepted) return {State::ExternalBlocked, "LEAP_OBSERVATION_STALE"};
        context_.business_event("leap.intent", receipt);
        return done();
    }
    const auto confirmation = context_.recognize(frame,
        recognition::parse_request(parameters.at("confirmation")));
    require_valid(confirmation);
    if (confirmation.basis.frame_id != frame.identity.frame_id ||
        !same_context(confirmation.basis, frame.identity))
        throw std::runtime_error("WVD_OPERATION_OBSERVATION_MISMATCH");
    if (confirmation.outcome != Outcome::Hit) return waiting();
    if (!current(confirmation)) return {State::ExternalBlocked, "WVD_OPERATION_STALE"};

    if (binding == "WvdConfirm") {
        const auto event = parameters.at("event").get<std::string>();
        const auto operation = parameters.at("operation").get<std::string>();
        std::optional<std::size_t> expected_step, reward_index;
        if (parameters.contains("expected_step") && !parameters.at("expected_step").is_null()) {
            const auto step = parameters.at("expected_step");
            if (!step.is_number_integer() || step < 0 || step > 4096)
                throw std::runtime_error("BUSINESS_TASK_STEP_INVALID");
            expected_step = step.get<std::size_t>();
        }
        if (event == "mining_reward_observed" || event == "fishing_reward_prepared") {
            const auto mode = event == "mining_reward_observed" ? "mining_reward" : "fishing_reward";
            if (parameters.at("confirmation").at("parameters").value("mode", "") != mode)
                throw std::runtime_error("BUSINESS_REWARD_RECOGNITION_REQUIRED");
            reward_index = confirmation.evidence.at("evidence").at("selected_index").get<std::size_t>();
        }
        J receipt;
        const bool accepted = state_.apply([&](contracts::BusinessRunState &base) {
            if (!current(confirmation)) return false;
            auto &state = dynamic_cast<WvdRunState &>(base);
            const auto id = state.confirmation_id(operation, event);
            const bool applied = state.confirm_event(id, event,
                confirmation.basis.generation, confirmation.basis.frame_id,
                expected_step, reward_index);
            receipt = {{"operation_id", id}, {"event", event}, {"applied", applied},
                {"frame_id", confirmation.basis.frame_id},
                {"generation", confirmation.basis.generation}};
            return true;
        });
        if (!accepted) return {State::ExternalBlocked, "BUSINESS_CONFIRMATION_STALE"};
        context_.business_event("confirmed", receipt);
        return done();
    }
    if (binding == "WvdCombat") {
        const auto operation = parameters.at("operation").get<std::string>();
        std::vector<PortraitScore> scores;
        if (operation == "prepare") {
            for (const auto &candidate : parameters.at("portraits")) {
                if (context_.cancelled()) return {State::ExternalBlocked, "CANCELLED"};
                const auto image = candidate.at("image");
                const auto observation = observe(frame,
                    {{"mode", "portrait"}, {"image", image}}, "wvd.combat.portrait");
                scores.push_back({candidate.at("role"),
                    observation.evidence.at("evidence").at("best_score")});
            }
        } else if (operation != "success" && operation != "auto_confirmed") {
            throw std::runtime_error("COMBAT_OPERATION_UNKNOWN");
        }
        J receipt;
        const bool accepted = state_.apply([&](contracts::BusinessRunState &base) {
            if (!current(confirmation)) return false;
            auto &state = dynamic_cast<WvdRunState &>(base);
            if (operation == "prepare") state.prepare_skill(scores, parameters.at("catalog"));
            else state.finish_prepared_skill(parameters.at("index").get<std::size_t>(),
                operation == "success" ? SkillOutcome::Succeeded :
                    SkillOutcome::AutoFallbackConfirmed);
            receipt = {{"operation", operation}, {"frame_id", frame.identity.frame_id},
                {"generation", frame.identity.generation}, {"consumed", operation != "prepare"}};
            return true;
        });
        if (!accepted) return {State::ExternalBlocked, "COMBAT_CONFIRMATION_STALE"};
        context_.business_event("combat", receipt);
        return done();
    }
    if (binding == "WvdChest") {
        std::array<bool, 6> fear{};
        for (unsigned i = 0; i < fear.size(); ++i) {
            if (context_.cancelled()) return {State::ExternalBlocked, "CANCELLED"};
            const int x = 258 + (i % 3) * 258, y = 1161 + (i / 3) * 184;
            const auto observation = observe(frame,
                {{"mode", "template"}, {"image", parameters.at("image")},
                 {"roi", {x - 125, y - 82, 250, 164}}},
                "wvd.chest.fear." + std::to_string(i));
            fear[i] = observation.outcome == Outcome::Hit;
        }
        J receipt;
        const bool accepted = state_.apply([&](contracts::BusinessRunState &base) {
            if (!current(confirmation)) return false;
            auto &state = dynamic_cast<WvdRunState &>(base);
            state.prepare_chest_character(fear, parameters.at("preferred").get<int>(),
                parameters.at("seed").get<std::uint32_t>());
            const auto summary = state.summary();
            receipt = {{"selected", summary.at("chest_character")},
                {"available_mask", summary.at("chest_available_mask")},
                {"frame_id", frame.identity.frame_id},
                {"generation", frame.identity.generation}};
            return true;
        });
        if (!accepted) return {State::ExternalBlocked, "CHEST_CONFIRMATION_STALE"};
        context_.business_event("chest_selection", receipt);
        return done();
    }
    throw std::runtime_error("WVD_OPERATION_UNREGISTERED:" + binding);
}
runtime::NativeExecutionSession::OperationFactory wvd_operation_factory(
    WvdRunState &state,
    std::function<void(const std::string &, const nlohmann::json &)> business_event,
    std::function<void(const std::string &)> checkpoint) {
    return [&state, business_event = std::move(business_event),
            checkpoint = std::move(checkpoint)](runtime::NativeFlowPorts &ports) {
        auto operations = std::make_shared<NativeOperations>(state, OperationContext{
            [&ports] { return ports.capture(); },
            [&ports](const auto &frame, const auto &request) {
                return ports.recognize(frame, request);
            },
            [&ports] { return ports.current_identity(); },
            [&ports] { return ports.cancelled(); },
            business_event, checkpoint});
        return [operations](const std::string &binding, const nlohmann::json &parameters,
            const std::optional<contracts::FrameEnvelope> &frame,
            const std::optional<contracts::Observation> &observation,
            const std::string &source_path) {
            return operations->execute(binding, parameters, frame, observation, source_path);
        };
    };
}
} // namespace wvd::games
