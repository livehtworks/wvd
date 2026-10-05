#pragma once
#include "pipeline_compiler.hpp"

namespace wvd::games::tasks {
// 在发布前固定内置任务的游戏素材语言；不改变运行中的识别策略。
void localize_task_assets(CompiledWorkflow &workflow, const nlohmann::json &catalogue,
                          const std::string &locale);
// Production entrypoints validate the complete frozen dependency list, including implicit probes.
nlohmann::json locale_asset_coverage(const CompiledWorkflow &workflow, const std::string &locale);
void require_locale_asset_coverage(const CompiledWorkflow &workflow, const std::string &locale);
// The same immutable recipes resolve hidden native probes and their packaged dependencies.
nlohmann::json localize_implicit_probe(const nlohmann::json &condition, const std::string &locale);
bool random_maze_probe(const nlohmann::json &condition);
}
