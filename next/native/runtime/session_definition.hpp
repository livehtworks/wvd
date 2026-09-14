#pragma once
#include "contracts/behavior.hpp"
#include "maafw/recognition.hpp"
#include "devices/lifecycle.hpp"

namespace wvd::runtime {
// 请求只携带不可变值；实现由应用封存的注册表解析，不接收临时 lambda 或配置引用。
struct SessionDefinition {
    maafw::Bundle bundle;
    std::string entry, terminal_node;
    contracts::BehaviorBindings actions;
    std::chrono::milliseconds time_limit{60000}, stop_timeout{3000};
    contracts::BehaviorBindings recognitions;
    std::string checkpoint_node;
    std::optional<devices::LifecyclePlan> lifecycle;
};
} // namespace wvd::runtime
