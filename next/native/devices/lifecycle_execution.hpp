#pragma once
#include "lifecycle.hpp"
#include <json.hpp>

namespace wvd::devices {
enum class LifecycleEnd { ReadyForBoot, RetryRequired, Cancelled };
void validate_lifecycle_plan(const LifecyclePlan &plan);
nlohmann::json lifecycle_plan_json(const LifecyclePlan &plan);
// 只有上一 Session 真静止后的新恢复段调用；返回 ReadyForBoot 不等于游戏已就绪。
LifecycleEnd execute_lifecycle_plan(
    const LifecyclePlan &plan, LifecyclePort &port, const std::function<bool()> &cancelled,
    const std::function<void(const std::string &, const nlohmann::json &)> &event);
}
