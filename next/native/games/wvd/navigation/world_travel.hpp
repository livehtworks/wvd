#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "games/wvd/tasks/task_plan.hpp"

namespace wvd::games::navigation {
enum class WorldArrival { City, DungeonEntrance };
// 只操作已知的地图/入口锚点；不调整世界地图缩放，不在未知转场里盲点。
tasks::CompiledWorkflow travel_world(const WorldDestination &destination, WorldArrival arrival);
// 起点可用公共城市锚点打开大地图；终点按目的地确认。王城必须命中塔楼背景，
// 不能用各城市共用的 Inn/guild 图标冒充到达。
tasks::CompiledWorkflow travel_city_to_city(const WorldDestination &destination);
}
