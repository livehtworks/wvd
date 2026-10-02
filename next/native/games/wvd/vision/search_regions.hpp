#pragma once
#include <json.hpp>

namespace wvd::games::vision {
// 只用于已确认的敌方选择阶段；覆盖远近敌人和两侧边缘，不包含底部技能/队伍。
inline nlohmann::json combat_target_search_roi() { return {0, 80, 900, 860}; }

// 当前行动者可能横移，但排队头像不是当前角色。只搜最上方头像行，不能搜整条行动栏。
inline nlohmann::json active_actor_search_roi() { return {0, 35, 250, 120}; }
} // namespace wvd::games::vision
