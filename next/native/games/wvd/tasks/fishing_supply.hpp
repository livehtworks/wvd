#pragma once
#include "pipeline_compiler.hpp"
#include "task_plan.hpp"
#include <set>

namespace wvd::games::tasks {
CompiledWorkflow fishing_cycle(const WvdQuestDefinition &, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
}
