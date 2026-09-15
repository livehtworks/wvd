#pragma once
#include "dungeon_route.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
WvdTaskPlan scorpion_plan(const WvdQuestDefinition &, bool hands_route = false);
CompiledWorkflow scorpion_cycle(const WvdQuestDefinition &, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
void configure_scorpion_units(runtime::RunDefinition &, bool hands, std::size_t cycles = 1);
}
