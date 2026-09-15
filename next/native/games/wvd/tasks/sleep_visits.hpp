#pragma once
#include "pipeline_compiler.hpp"
#include "quest_catalog.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
CompiledWorkflow sleep_visits(const WvdQuestDefinition &, const nlohmann::json &profile);
void configure_sleep_units(runtime::RunDefinition &);
}
