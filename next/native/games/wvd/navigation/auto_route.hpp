#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::navigation {
// 自动搜索的完成条件是明确的无目标提示；停止运动仅返回外层重观察，不提交任务步。
tasks::CompiledWorkflow auto_route(const std::string &target);
}
