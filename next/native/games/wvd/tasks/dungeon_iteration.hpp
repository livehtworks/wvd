#pragma once
#include "dungeon_route.hpp"

namespace wvd::games::tasks {
// 一次正常 Farm 迭代：观察入口 -> 必要补给 -> EOT -> StateDungeon。
// 完成一段不代表整个 TaskID 已验收，也不把恢复需求当作普通下一段。
CompiledWorkflow dungeon_iteration(const WvdTaskPlan &, const nlohmann::json &profile,
                                    const std::set<std::string> &available_images);
}
