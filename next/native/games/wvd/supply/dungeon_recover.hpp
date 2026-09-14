#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::supply {
// 只编译角色面板恢复；是否需要恢复由本 Run 的配置/遭遇事实判断，不负责住宿。
tasks::CompiledWorkflow recover_in_dungeon();
}
