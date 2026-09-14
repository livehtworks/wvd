#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "runtime/behavior_registry.hpp"
#include <set>

namespace wvd::games::combat {
// 编译一个角色的有限动作；整场战斗由外层重新观察，不保存旧坐标或批量点下一角色。
tasks::CompiledWorkflow take_turn(const nlohmann::json &profile,
                                  const std::set<std::string> &available_images);
void register_combat(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding combat_binding();
}
