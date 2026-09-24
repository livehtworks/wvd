#pragma once
#include "dungeon_route.hpp"
namespace wvd::games::tasks {
WvdTaskPlan bull_cave_plan(const WvdQuestDefinition &definition);
CompiledWorkflow bull_cave_cycle(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
}
