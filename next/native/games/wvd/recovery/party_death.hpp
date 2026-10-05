#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::recovery {
// 救人页的新帧门控中心连点；退出后由原再起/全队死亡处理器继续。
tasks::CompiledWorkflow dismiss_party_death();
tasks::CompiledWorkflow acknowledge_party_defeat();
}
