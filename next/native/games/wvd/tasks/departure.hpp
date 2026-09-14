#pragma once
#include "pipeline_compiler.hpp"
#include "task_plan.hpp"

namespace wvd::games::tasks {
// 只处理已离开地下城后的回城/补给，到可开始 EOT 的位置结束，不冒充一轮任务。
CompiledWorkflow prepare_departure(const WvdTaskPlan &, const nlohmann::json &profile,
                                   bool force_rest = false);
}
