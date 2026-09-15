#pragma once
#include "pipeline_compiler.hpp"
#include "quest_catalog.hpp"
#include <set>

namespace wvd::games::tasks {
// 暗灯本内循环；不自动入本或传送，不套普通副本任务点计数。
CompiledWorkflow dark_light(const WvdQuestDefinition &definition, const nlohmann::json &profile,
                            const std::set<std::string> &images, bool allow_download = true);
}
