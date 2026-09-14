#pragma once
#include "games/wvd/tasks/task_plan.hpp"
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::navigation {
tasks::CompiledWorkflow enter_dungeon(const WvdTaskPlan &plan);
}
