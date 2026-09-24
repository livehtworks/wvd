#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
WvdTaskPlan fordraig_plan(const WvdQuestDefinition &definition);
std::vector<CompiledWorkflow> fordraig_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
}
