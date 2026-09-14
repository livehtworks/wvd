#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::recovery {
enum class GlobalPrompt { SandmanRecovery, Blessing };
tasks::CompiledWorkflow dismiss_global_prompt(GlobalPrompt prompt);
}
