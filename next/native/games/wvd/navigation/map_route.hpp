#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "games/wvd/tasks/task_plan.hpp"

namespace wvd::games::navigation {
// 一个地图目标的有限尝试。完成须有到点证据；战斗/宝箱交回外层，不提交任务步。
tasks::CompiledWorkflow reach_map_target(const MapTarget &target,
                                         const std::optional<std::string> &floor = {});
}
