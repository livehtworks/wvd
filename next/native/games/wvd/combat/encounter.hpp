#pragma once
#include "turn.hpp"

namespace wvd::games::combat {
// 一场有界战斗；次数是明确发布参数，耗尽转恢复而不是假报完成。
tasks::CompiledWorkflow fight_encounter(const nlohmann::json &profile,
                                         const std::set<std::string> &available_images,
                                         unsigned max_turns, unsigned max_auto_polls = 128);
}
