#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
WvdTaskPlan golden_chest_plan(const WvdQuestDefinition &definition);
CompiledWorkflow golden_chest_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
}
