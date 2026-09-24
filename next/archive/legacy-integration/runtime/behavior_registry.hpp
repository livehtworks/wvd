#pragma once
#include "contracts/run.hpp"
#include "contracts/business_state.hpp"
#include "maafw/gateway.hpp"
#include "session_definition.hpp"

namespace wvd::runtime {
class BehaviorRegistry {
  public:
    // 函数指针有意拒绝捕获 lambda。配置从冻结参数读取；测试故障注入另在测试后端声明。
    using Action = bool (*)(maafw::Context &, const nlohmann::json &, const nlohmann::json &);
    using Recognition = nlohmann::json (*)(const maafw::Bundle &, maafw::RecognitionPixels,
                                           const nlohmann::json &, const nlohmann::json &,
                                           const maafw::CustomRecognitionScope &,
                                           maafw::RecognitionCache &);
    using Recovery = std::optional<SessionDefinition> (*)(const contracts::SessionResult &,
                                                          const SessionDefinition &,
                                                          const nlohmann::json &);
    using StateFactory = std::unique_ptr<contracts::BusinessRunState> (*)(
        const nlohmann::json &, const contracts::StateCreationContext &);
    explicit BehaviorRegistry(std::string revision);
    void add_action(contracts::ImplementationIdentity identity, Action action);
    void add_recovery(contracts::ImplementationIdentity identity, Recovery recovery);
    void add_recognition(contracts::ImplementationIdentity identity, Recognition recognition);
    void add_state_factory(contracts::ImplementationIdentity identity, StateFactory factory);
    void validate_state_factory(const contracts::BehaviorBinding &binding) const;
    std::unique_ptr<contracts::BusinessRunState>
    create_state(const contracts::BehaviorBinding &binding,
                 const contracts::StateCreationContext &creation) const;
    maafw::RecognitionHandlers bind_recognitions(const contracts::BehaviorBindings &bindings) const;
    void seal();
    bool sealed() const { return sealed_; }
    nlohmann::json manifest() const;
    void validate(const SessionDefinition &definition) const;
    void validate_recovery(const contracts::BehaviorBinding &binding) const;
    maafw::ActionRegistry bind(const contracts::BehaviorBindings &bindings) const;
    std::optional<SessionDefinition> recover(const contracts::BehaviorBinding &binding,
                                             const contracts::SessionResult &result,
                                             const SessionDefinition &previous) const;

  private:
    const std::string revision_;
    bool sealed_{};
    std::map<contracts::ImplementationIdentity, Action> actions_;
    std::map<contracts::ImplementationIdentity, Recovery> recoveries_;
    std::map<contracts::ImplementationIdentity, Recognition> recognitions_;
    std::map<contracts::ImplementationIdentity, StateFactory> state_factories_;
};
nlohmann::json binding_json(const contracts::BehaviorBinding &binding);
nlohmann::json session_definition_json(const SessionDefinition &definition,
                                       bool include_files = true);
} // namespace wvd::runtime
