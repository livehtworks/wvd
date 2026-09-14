#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::supply {
// 住宿终点为确认住宿后返回城市，不把重组队伍当成补充镐子。
tasks::CompiledWorkflow rest_at_inn(bool royal_suite);
} // namespace wvd::games::supply
