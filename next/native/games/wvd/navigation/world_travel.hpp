#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "games/wvd/tasks/task_plan.hpp"

namespace wvd::games::navigation {
enum class WorldArrival { City, DungeonEntrance };
// 只操作已知的地图/入口锚点；不调整世界地图缩放，不在未知转场里盲点。
tasks::CompiledWorkflow travel_world(const WorldDestination &destination, WorldArrival arrival);
}
