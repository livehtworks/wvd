#pragma once
#include "pipeline_compiler.hpp"
#include "runtime/behavior_registry.hpp"

namespace wvd::games::tasks {
// 只发布到不存在的新目录；资产清单、行为注册表和编译图全部通过后才创建 Session。
runtime::SessionDefinition publish_workflow(const CompiledWorkflow &workflow,
                                            const maafw::Bundle &source,
                                            const runtime::BehaviorRegistry &registry,
                                            const std::filesystem::path &destination,
                                            const nlohmann::json &aliases);
} // namespace wvd::games::tasks
