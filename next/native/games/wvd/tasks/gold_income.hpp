#pragma once
#include "pipeline_compiler.hpp"
#include "quest_catalog.hpp"
namespace wvd::games::tasks {
CompiledWorkflow gold_income_cycle(const WvdQuestDefinition &definition, bool allow_download = true);
}
