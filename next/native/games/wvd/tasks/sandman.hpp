#pragma once
#include "dungeon_route.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
WvdTaskPlan sandman_plan(const WvdQuestDefinition &definition);
CompiledWorkflow sandman_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
void configure_sandman_units(runtime::RunDefinition &definition, std::size_t visits = 1);
}
