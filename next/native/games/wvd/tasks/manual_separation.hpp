#pragma once
#include "dungeon_route.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::tasks {
WvdTaskPlan manual_separation_plan(const WvdQuestDefinition &, bool second_route);
// 同一封存图内两条有限业务段，由唯一 RunCoordinator 在真正静止后续段。
CompiledWorkflow manual_separation(const WvdQuestDefinition &, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download = true);
void configure_manual_separation_units(runtime::RunDefinition &);
}
