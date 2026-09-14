#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::supply {
// 住宿终点为确认住宿后返回城市，不把重组队伍当成补充镐子。
// record_completion 供持有 WvdRunState 的完整业务使用；独立 UI 子流程不写运行状态。
tasks::CompiledWorkflow rest_at_inn(bool royal_suite, bool record_completion = false);
} // namespace wvd::games::supply
