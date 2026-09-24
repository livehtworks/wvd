#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
WvdTaskPlan repel_forces_plan(const WvdQuestDefinition &definition);
CompiledWorkflow repel_forces_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
}
