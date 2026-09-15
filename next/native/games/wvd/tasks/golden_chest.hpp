#pragma once
#include "dungeon_route.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
WvdTaskPlan golden_chest_plan(const WvdQuestDefinition &definition);
CompiledWorkflow golden_chest_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
void configure_golden_chest_units(runtime::RunDefinition &definition, std::size_t cycles = 1);
}
