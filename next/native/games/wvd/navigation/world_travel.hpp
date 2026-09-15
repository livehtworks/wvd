#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "games/wvd/tasks/task_plan.hpp"

namespace wvd::games::navigation {
enum class WorldArrival { City, DungeonEntrance };
// 只操作已知的地图/入口锚点；不调整世界地图缩放，不在未知转场里盲点。
tasks::CompiledWorkflow travel_world(const WorldDestination &destination, WorldArrival arrival);
// 起点和终点都有Inn。必须先确认进入大地图，才可使用入城终点；不能直接套返回城内判定。
tasks::CompiledWorkflow travel_city_to_city(const WorldDestination &destination);
}
