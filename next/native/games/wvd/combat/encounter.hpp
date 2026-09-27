#pragma once
#include "turn.hpp"

namespace wvd::games::combat {
enum class EncounterEnd { Dungeon, RepelPrompt };
// 以战斗的真实结束为边界；角色正常行动不消耗失败次数。
tasks::CompiledWorkflow fight_encounter(const nlohmann::json &profile,
                                         const std::set<std::string> &available_images,
                                         EncounterEnd end = EncounterEnd::Dungeon);
}
