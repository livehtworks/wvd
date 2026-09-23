#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::navigation {
// 从哈肯祝福或楼层页逐级返回城市；不负责在副本里寻找哈肯。
tasks::CompiledWorkflow leave_harken();
}
