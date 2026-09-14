#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::recovery {
// someonedead 提示的有限消费者，不承担 RiseAgain、全队死亡或住宿。
tasks::CompiledWorkflow dismiss_party_death();
}
