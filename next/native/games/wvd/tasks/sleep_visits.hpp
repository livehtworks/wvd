#pragma once
#include "pipeline_compiler.hpp"
#include "quest_catalog.hpp"

namespace wvd::games::tasks {
CompiledWorkflow sleep_visits(const WvdQuestDefinition &, const nlohmann::json &profile);
}
