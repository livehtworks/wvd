#pragma once
#include "games/wvd/profile.hpp"
#include "quest_catalog.hpp"
#include "runtime/run_coordinator.hpp"
#include <mutex>

namespace wvd::games::tasks {
// 来源只是当前Run的冻结事实，不是待启动请求。请求由TaskHandoff::poll在保存后创建。
nlohmann::json freeze_handoff_source(const runtime::RunDefinition &definition,
    const WvdProfile &profile, const WvdQuestDefinition &task, const WvdQuestCatalog &catalog);
void validate_handoff_source(const nlohmann::json &source, const nlohmann::json &profile);
bool handoff_has_unconfirmed_effect(const nlohmann::json &business);
void register_task_handoff(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding unknown_leap_binding();
// 仅从已走完正常分类的unknown分支调用；读取既有unknown.window，不另增样本。
// 命中返回true后应接RequireRecovery(reason="leap.unknown")，未命中返回原unknown链。
bool observe_unknown_leap(maafw::Context &context);

class TaskHandoff {
  public:
    TaskHandoff(runtime::RunCoordinator &coordinator,
                std::shared_ptr<const runtime::BehaviorRegistry> registry,
                maafw::Bundle assets, nlohmann::json aliases = nlohmann::json::object(),
                std::optional<maafw::Bundle> mod = {}, bool allow_download = false);
    contracts::RunSnapshot start_source(runtime::RunDefinition definition, const WvdProfile &profile,
        const WvdQuestDefinition &task, const WvdQuestCatalog &catalog,
        std::shared_ptr<devices::DeviceBackend> backend);
    // 无后台线程/第二runner。由现有服务命令循环轮询，UI只观察，不自行启动任务。
    std::optional<contracts::RunSnapshot> poll(const std::filesystem::path &new_bundle_directory);
    void request_stop();

  private:
    runtime::RunCoordinator &coordinator_;
    const std::shared_ptr<const runtime::BehaviorRegistry> registry_;
    const maafw::Bundle assets_;
    const nlohmann::json aliases_;
    const std::optional<maafw::Bundle> mod_;
    const bool allow_download_;
    std::mutex mutex_;
    std::mutex start_stop_mutex_;
    std::atomic<bool> stopped_{false};
    std::shared_ptr<devices::DeviceBackend> backend_;
    std::optional<runtime::RunDefinition> source_, next_;
    std::uint64_t source_run_{};
    nlohmann::json provenance_;
};
} // namespace wvd::games::tasks
