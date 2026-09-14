#include "behavior_registry.hpp"
#include "core_build_id.hpp"
#include <set>

namespace wvd::runtime {
using J = nlohmann::json;
namespace {
void identity_valid(const contracts::ImplementationIdentity &identity) {
    if (identity.id.empty() || identity.revision.empty() || identity.id.size() > 128 ||
        identity.revision.size() > 128)
        throw std::runtime_error("IMPLEMENTATION_IDENTITY_INVALID");
}
void binding_valid(const contracts::BehaviorBinding &binding) {
    identity_valid(binding.implementation);
    if (binding.name.empty() || binding.name.size() > 128 || !binding.parameters.is_object() ||
        binding.parameters.dump().size() > 32768)
        throw std::runtime_error("BEHAVIOR_BINDING_INVALID");
}
const std::set<std::string> reserved{"RootTerminal", "RunChild", "RequireRecovery", "GuardedAction",
                                     "BusinessCheckpoint"};
} // namespace
BehaviorRegistry::BehaviorRegistry(std::string revision) : revision_(std::move(revision)) {
    if (revision_.empty())
        throw std::runtime_error("REGISTRY_REVISION_REQUIRED");
}
void BehaviorRegistry::add_action(contracts::ImplementationIdentity identity, Action action) {
    if (sealed_)
        throw std::runtime_error("REGISTRY_SEALED");
    identity_valid(identity);
    if (!action || !actions_.emplace(std::move(identity), action).second)
        throw std::runtime_error("IMPLEMENTATION_DUPLICATE_OR_EMPTY");
}
void BehaviorRegistry::add_recovery(contracts::ImplementationIdentity identity, Recovery recovery) {
    if (sealed_)
        throw std::runtime_error("REGISTRY_SEALED");
    identity_valid(identity);
    if (!recovery || !recoveries_.emplace(std::move(identity), recovery).second)
        throw std::runtime_error("IMPLEMENTATION_DUPLICATE_OR_EMPTY");
}
void BehaviorRegistry::seal() { sealed_ = true; }
void BehaviorRegistry::add_state_factory(contracts::ImplementationIdentity identity,
                                         StateFactory factory) {
    if (sealed_)
        throw std::runtime_error("REGISTRY_SEALED");
    identity_valid(identity);
    if (!factory || !state_factories_.emplace(std::move(identity), factory).second)
        throw std::runtime_error("IMPLEMENTATION_DUPLICATE_OR_EMPTY");
}
void BehaviorRegistry::validate_state_factory(const contracts::BehaviorBinding &binding) const {
    if (!sealed_)
        throw std::runtime_error("REGISTRY_NOT_SEALED");
    binding_valid(binding);
    if (!state_factories_.contains(binding.implementation))
        throw std::runtime_error("STATE_FACTORY_UNKNOWN");
}
std::unique_ptr<contracts::BusinessRunState>
BehaviorRegistry::create_state(const contracts::BehaviorBinding &binding,
                               const contracts::StateCreationContext &creation) const {
    validate_state_factory(binding);
    auto state = state_factories_.at(binding.implementation)(binding.parameters, creation);
    if (!state)
        throw std::runtime_error("STATE_FACTORY_RETURNED_EMPTY");
    return state;
}
void BehaviorRegistry::add_recognition(contracts::ImplementationIdentity identity,
                                       Recognition recognition) {
    if (sealed_)
        throw std::runtime_error("REGISTRY_SEALED");
    identity_valid(identity);
    if (!recognition || !recognitions_.emplace(std::move(identity), recognition).second)
        throw std::runtime_error("IMPLEMENTATION_DUPLICATE_OR_EMPTY");
}
maafw::RecognitionHandlers
BehaviorRegistry::bind_recognitions(const contracts::BehaviorBindings &bindings) const {
    SessionDefinition definition;
    definition.recognitions = bindings;
    validate(definition);
    maafw::RecognitionHandlers result;
    for (const auto &binding : bindings) {
        auto function = recognitions_.at(binding.implementation);
        result.emplace(binding.name, [function, binding](const maafw::Bundle &bundle,
                                                         maafw::RecognitionPixels pixels,
                                                         const J &node,
                                                         const maafw::CustomRecognitionScope &scope,
                                                         maafw::RecognitionCache &cache) {
            auto result = function(bundle, pixels, node, binding.parameters, scope, cache);
            result["binding"] = binding_json(binding);
            return result;
        });
    }
    return result;
}
J BehaviorRegistry::manifest() const {
    if (!sealed_)
        throw std::runtime_error("REGISTRY_NOT_SEALED");
    J entries = J::array();
    for (const auto &[identity, factory] : state_factories_)
        entries.push_back(
            {{"kind", "state_factory"}, {"id", identity.id}, {"revision", identity.revision}});
    for (const auto &[identity, recognition] : recognitions_)
        entries.push_back(
            {{"kind", "recognition"}, {"id", identity.id}, {"revision", identity.revision}});
    for (const auto &[identity, action] : actions_)
        entries.push_back(
            {{"kind", "action"}, {"id", identity.id}, {"revision", identity.revision}});
    for (const auto &[identity, recovery] : recoveries_)
        entries.push_back(
            {{"kind", "recovery"}, {"id", identity.id}, {"revision", identity.revision}});
    return {{"build_id", WVD_CORE_BUILD_ID},
            {"registry_revision", revision_},
            {"builtin_revision", "runtime-3"},
            {"entries", entries}};
}
void BehaviorRegistry::validate(const SessionDefinition &definition) const {
    if (!sealed_)
        throw std::runtime_error("REGISTRY_NOT_SEALED");
    std::set<std::string> names;
    for (const auto &binding : definition.actions) {
        binding_valid(binding);
        if (reserved.contains(binding.name))
            throw std::runtime_error("RESERVED_ACTION_OVERRIDE");
        if (!names.insert(binding.name).second)
            throw std::runtime_error("ACTION_BINDING_DUPLICATE");
        if (!actions_.contains(binding.implementation))
            throw std::runtime_error("ACTION_IMPLEMENTATION_UNKNOWN");
    }
    names.clear();
    for (const auto &binding : definition.recognitions) {
        binding_valid(binding);
        if (!names.insert(binding.name).second)
            throw std::runtime_error("RECO_BINDING_DUPLICATE");
        if (!recognitions_.contains(binding.implementation))
            throw std::runtime_error("RECO_IMPLEMENTATION_UNKNOWN");
    }
}
void BehaviorRegistry::validate_recovery(const contracts::BehaviorBinding &binding) const {
    if (!sealed_)
        throw std::runtime_error("REGISTRY_NOT_SEALED");
    binding_valid(binding);
    if (!recoveries_.contains(binding.implementation))
        throw std::runtime_error("RECOVERY_IMPLEMENTATION_UNKNOWN");
}
maafw::ActionRegistry BehaviorRegistry::bind(const contracts::BehaviorBindings &bindings) const {
    SessionDefinition definition;
    definition.actions = bindings;
    validate(definition);
    maafw::ActionRegistry handlers;
    for (const auto &binding : bindings) {
        auto implementation = actions_.at(binding.implementation);
        handlers.emplace(binding.name, [implementation, parameters = binding.parameters](
                                           maafw::Context &context, const J &node) {
            return implementation(context, node, parameters);
        });
    }
    return handlers;
}
std::optional<SessionDefinition>
BehaviorRegistry::recover(const contracts::BehaviorBinding &binding,
                          const contracts::SessionResult &result,
                          const SessionDefinition &previous) const {
    validate_recovery(binding);
    auto decision = recoveries_.at(binding.implementation)(result, previous, binding.parameters);
    if (decision)
        validate(*decision);
    return decision;
}
J binding_json(const contracts::BehaviorBinding &binding) {
    return {{"name", binding.name},
            {"implementation_id", binding.implementation.id},
            {"implementation_revision", binding.implementation.revision},
            {"parameters", binding.parameters}};
}
J session_definition_json(const SessionDefinition &definition, bool include_files) {
    J actions = J::array();
    for (const auto &binding : definition.actions)
        actions.push_back(binding_json(binding));
    J result{{"pack_revision", definition.bundle.revision},
             {"entry", definition.entry},
             {"terminal", definition.terminal_node},
             {"checkpoint", definition.checkpoint_node},
             {"time_limit_ms", definition.time_limit.count()},
             {"stop_timeout_ms", definition.stop_timeout.count()},
             {"custom_actions", actions}};
    result["custom_recognitions"] = J::array();
    for (const auto &binding : definition.recognitions)
        result["custom_recognitions"].push_back(binding_json(binding));
    if (include_files) {
        result["files"] = J::array();
        for (const auto &file : definition.bundle.files)
            result["files"].push_back({{"path", file.relative_path}, {"sha256", file.sha256}});
    }
    return result;
}
} // namespace wvd::runtime
