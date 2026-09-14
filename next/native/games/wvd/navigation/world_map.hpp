#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::navigation {
// 本段要求已打开世界地图且目标可见；打开地图/缩放/找目标由外层独立段承担。
tasks::CompiledWorkflow enter_city(const std::string &city);
} // namespace wvd::games::navigation
