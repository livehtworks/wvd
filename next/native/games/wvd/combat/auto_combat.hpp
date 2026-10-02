#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::combat {
// 单独有限段：关闭遮挡，确认 Auto 已开启。不是整场战斗完成，也不消费技能条目。
tasks::CompiledWorkflow enable_auto();
// 单角色保底：灯亮不等于攻击开始，待指令菜单退出后才关闭自动战斗。
tasks::CompiledWorkflow single_actor_auto();
} // namespace wvd::games::combat
