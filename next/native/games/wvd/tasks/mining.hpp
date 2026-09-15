#pragma once
#include "pipeline_compiler.hpp"
#include "task_plan.hpp"
#include <set>

namespace wvd::games::tasks {
CompiledWorkflow mining_iteration(const WvdQuestDefinition &definition, const nlohmann::json &profile,
                                  bool allow_download = true);
}
