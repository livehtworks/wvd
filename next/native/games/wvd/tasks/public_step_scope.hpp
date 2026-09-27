#pragma once
#include "pipeline_compiler.hpp"
#include <functional>

namespace wvd::games::tasks {
// 只在 Application 的同步编译调用栈内借用已冻结的作者库；不持有运行状态、
// 不回读文件、不创建线程。没有装配该依赖时拒绝编译，绝不回退到另一套步骤。
class PublicStepScope {
  public:
    using Resolver = std::function<CompiledWorkflow(const std::string &, const nlohmann::json &)>;
    explicit PublicStepScope(Resolver resolver) : resolver_(std::move(resolver)), previous_(current_) {
        current_ = &resolver_;
    }
    ~PublicStepScope() { current_ = previous_; }
    PublicStepScope(const PublicStepScope &) = delete;
    PublicStepScope &operator=(const PublicStepScope &) = delete;
    static CompiledWorkflow compile(const std::string &id, const nlohmann::json &arguments) {
        if (!current_) throw std::runtime_error("PUBLIC_STEP_LIBRARY_NOT_ASSEMBLED:" + id);
        return (*current_)(id, arguments);
    }
  private:
    Resolver resolver_;
    const Resolver *previous_;
    inline static thread_local const Resolver *current_{};
};
inline const std::vector<std::string> native_public_steps{
    "combat-open-detail", "combat-select-level", "combat-select-target", "combat-confirm-result",
    "chest-choose-character", "chest-disarm", "chest-reward-continue", "navigation-resume"};
} // namespace wvd::games::tasks
