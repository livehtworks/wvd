#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::recovery {
// 只处理已观察到的 RiseAgain；没有复活画面时不凭坐标猜测，也不冒充应用恢复。
tasks::CompiledWorkflow revive_after_defeat();
}
