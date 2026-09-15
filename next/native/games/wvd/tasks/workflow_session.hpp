#pragma once
#include "pipeline_compiler.hpp"
#include "runtime/behavior_registry.hpp"

namespace wvd::games::tasks {
// 只发布到不存在的新目录；资产清单、行为注册表和编译图全部通过后才创建 Session。
runtime::SessionDefinition publish_workflow(const CompiledWorkflow &workflow,
                                            const maafw::Bundle &source,
                                            const runtime::BehaviorRegistry &registry,
                                            const std::filesystem::path &destination,
                                            const nlohmann::json &aliases,
                                            const maafw::Bundle *mod = nullptr);
// 正常续段共享一次封存版本；图节点命名空间隔离，识别策略仍按各Session冻结。
std::vector<runtime::SessionDefinition> publish_workflow_stages(const std::vector<CompiledWorkflow> &stages,
                                            const maafw::Bundle &source,
                                            const runtime::BehaviorRegistry &registry,
                                            const std::filesystem::path &destination,
                                            const nlohmann::json &aliases,
                                            const maafw::Bundle *mod = nullptr);
} // namespace wvd::games::tasks
