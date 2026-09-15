#pragma once
#include "pipeline_compiler.hpp"
#include "task_plan.hpp"
#include <set>

namespace wvd::games::tasks {
enum class DungeonTaskStop { None, CaveEna, CaveRequest };
// 默认 None 保留已有调用语义；指定停点只结束子图，外层仍须新帧确认目标。
// 非法停点在业务图编译时拒绝，不能当作任务完成。
std::string dungeon_task_stop_image(DungeonTaskStop stop);
// 一次 StateDungeon 范围：路线、遭遇插入及角色恢复。外层入本/回城/住宿另行组合。
CompiledWorkflow traverse_dungeon(const WvdTaskPlan &plan, const nlohmann::json &profile,
                                 const std::set<std::string> &available_images, bool allow_download = true,
                                 recovery::DialoguePolicy dialogue = recovery::DialoguePolicy::Default,
                                 DungeonTaskStop task_stop = DungeonTaskStop::None);
}
