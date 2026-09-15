#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
WvdTaskPlan giant_plan(const WvdQuestDefinition &);
CompiledWorkflow giant_iteration(const WvdQuestDefinition &, const nlohmann::json &profile,
                                 const std::set<std::string> &images, bool allow_download = true);
}
