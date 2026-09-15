#pragma once
#include "dungeon_route.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
WvdTaskPlan repel_forces_plan(const WvdQuestDefinition &definition);
CompiledWorkflow repel_forces_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
void configure_repel_forces_units(runtime::RunDefinition &definition, const nlohmann::json &profile);
}
