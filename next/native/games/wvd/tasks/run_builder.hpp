#pragma once
#include "pipeline_compiler.hpp"
#include "quest_catalog.hpp"
#include "contracts/business_state.hpp"
#include "contracts/run.hpp"
#include "devices/backend.hpp"
#include <functional>
#include <set>

namespace wvd::games::tasks {
using J = nlohmann::json;
using BountyBuilder = std::function<CompiledWorkflow(const WvdQuestDefinition &, const J &,
    const std::set<std::string> &)>;

// 任务选择和轮数属于 WVD 业务，Application 只冻结输入并装配运行所有者。
CompiledWorkflow build_task_workflow(const WvdQuestDefinition &task, const J &values,
    const std::set<std::string> &images, const BountyBuilder &bounty);
std::size_t task_unit_count(const std::string &task_id, const J &values);
std::function<std::optional<devices::LifecyclePlan>(const contracts::SessionResult &,
    const contracts::BusinessRunState &, unsigned)> recovery_policy(devices::LifecycleTarget target);
bool handoff_ready(const contracts::SessionResult &last,
                   const contracts::BusinessRunState &state, const J &source);
} // namespace wvd::games::tasks
