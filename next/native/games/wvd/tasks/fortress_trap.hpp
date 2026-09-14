#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
WvdTaskPlan fortress_trap_plan(const WvdQuestDefinition &definition);
CompiledWorkflow fortress_trap_iteration(const WvdQuestDefinition &definition,
    const nlohmann::json &profile, const std::set<std::string> &images, bool allow_download = true);
}
