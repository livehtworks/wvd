#pragma once
#include "pipeline_compiler.hpp"
#include "task_plan.hpp"
#include <set>

namespace wvd::games::tasks {
// 一次 StateDungeon 范围：路线、遭遇插入及角色恢复。外层入本/回城/住宿另行组合。
CompiledWorkflow traverse_dungeon(const WvdTaskPlan &plan, const nlohmann::json &profile,
                                 const std::set<std::string> &available_images);
}
