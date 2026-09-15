#pragma once
#include "dungeon_route.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
WvdTaskPlan fordraig_plan(const WvdQuestDefinition &definition);
std::vector<CompiledWorkflow> fordraig_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
void configure_fordraig_units(runtime::RunDefinition &definition,
    const std::vector<runtime::SessionDefinition> &stages, std::size_t cycles = 1);
}
