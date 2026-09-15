#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
// steeltrail是旧源码case，不向基础58项目录注入新的TaskID。
WvdTaskPlan steel_trial_plan(const WvdQuestDefinition &);
CompiledWorkflow steel_trial_cycle(const WvdQuestDefinition &, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
}
