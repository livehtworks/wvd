#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
class PublicFlowLibrary;
WvdTaskPlan scorpion_plan(const WvdQuestDefinition &, bool hands_route = false);
WvdTaskPlan jier_plan(const WvdQuestDefinition &);
CompiledWorkflow bounty_cycle(const WvdQuestDefinition &, const nlohmann::json &profile,
    const std::set<std::string> &images, const PublicFlowLibrary &library,
    const nlohmann::json &board_root, const std::string &locale, bool allow_download = true);
}
