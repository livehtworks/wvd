#pragma once
#include "pipeline_compiler.hpp"

namespace wvd::games::tasks {
// 在发布前固定内置任务的游戏素材语言；不改变运行中的识别策略。
void localize_task_assets(CompiledWorkflow &workflow, const nlohmann::json &catalogue,
                          const std::string &locale);
}
