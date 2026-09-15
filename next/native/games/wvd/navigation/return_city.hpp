#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
namespace wvd::games::navigation {
// 跳跃后的返回要塞，仅确认Inn，不包含住宿或前往王城。
tasks::CompiledWorkflow return_to_fortress();
}
