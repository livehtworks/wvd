#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::combat {
// 单独有限段：关闭遮挡，确认 Auto 已开启。不是整场战斗完成，也不消费技能条目。
tasks::CompiledWorkflow enable_auto();
} // namespace wvd::games::combat
