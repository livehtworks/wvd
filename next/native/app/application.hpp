#pragma once

#include "api/routes.hpp"
#include "games/wvd/tasks/quest_catalog.hpp"
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "devices/device_session.hpp"
#include "runtime/native_run_coordinator.hpp"
#include "storage/profile_store.hpp"
#include "storage/workflow_repository.hpp"
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace wvd::app {
struct ApplicationPaths {
    std::filesystem::path data_root, pack_root, legacy_config, quest_catalog;
};

// Windows常驻应用唯一装配者。HTTP只调用此对象；设备、Run和可编辑数据均不归Vue所有。
class Application {
  public:
    explicit Application(ApplicationPaths paths,
                         std::shared_ptr<devices::DeviceConnection> connection = {});
    ~Application();
    api::DynamicReply handle(const api::Request &request);
    void request_shutdown();
    void stop();

  private:
    using J = nlohmann::json;
    J load_json(const std::filesystem::path &path) const;
    J profile() const;
    J effective_profile_values(const std::string &task_id) const;
    J effective_profile_values(const std::string &task_id, const J &stored) const;
    J queue_run(const std::string &kind, const J &request, const J &identity,
                std::function<J()> prepare);
    games::tasks::CompiledWorkflow compile_task_graph(const J &request,
        const games::WvdQuestDefinition &task, const J &values) const;
    runtime::NativeRunDefinition assemble_task(const J &request, const J &stored,
                                         const devices::LifecycleTarget &target,
                                         std::optional<J> frozen_values = std::nullopt,
                                         bool continuation = false,
                                         std::optional<games::tasks::CompiledWorkflow> prepared = std::nullopt);
    runtime::NativeRunDefinition assemble_workflow(const J &request, const J &stored, J document,
                                             const devices::LifecycleTarget &target,
                                             std::map<std::string, std::string> *pipeline_to_node = nullptr,
                                             const J &library_snapshot = J::object(),
                                             J *source_paths = nullptr);
    friend struct ApplicationAssemblyTestAccess;
    J prepare_task(const J &request, const J &stored,
                   std::shared_ptr<devices::DeviceConnection> backend,
                   std::optional<J> frozen_values = std::nullopt,
                   J handoff_parent = nullptr,
                   std::optional<games::tasks::CompiledWorkflow> prepared = std::nullopt);
    void watch_task_handoff(const J &stored, J source_values,
                            std::shared_ptr<devices::DeviceConnection> backend,
                            std::string request_id);
    J prepare_workflow(const std::string &flow_id, const J &request, const J &stored,
                       J document, std::shared_ptr<devices::DeviceConnection> backend,
                       const J &library_snapshot);
    J profile_for_task(const std::string &task_id) const;
    J catalog() const;
    J device_status() const;
    J run_status() const;
    api::DynamicReply diagnostic_image(const std::string &name) const;
    J save_profile(const J &request);
    J select_emulator_path() const;
    J connect_device(const J &request);
    // 仅在既有设备作业线程调用，同一实现服务于手动连接和开始任务的自动准备。
    void connect_selected_device(const J &request);
    std::shared_ptr<devices::DeviceConnection> ensure_connected_for_run(const J &stored);
    J disconnect_device();
    J capture_device();
    J start_task(const J &request);
    J list_workflows() const;
    J inspect_builtin(const std::string &flow_id) const;
    J sync_builtin(const std::string &flow_id, const J &request);
    J create_workflow(const J &request);
    J import_task_workflow(const J &request);
    J read_workflow(const std::string &flow_id) const;
    J save_workflow(const std::string &flow_id, const J &request);
    void delete_workflow(const std::string &flow_id, const J &request);
    J start_workflow(const std::string &flow_id, const J &request);
    J stop_run(std::optional<std::uint64_t> requested_run_id = std::nullopt,
               const std::string &requested_submission = {});
    J recognition_probe(const J &request);
    void start_device_job(std::string name, std::function<void()> job);
    bool run_active() const;

    ApplicationPaths paths_;
    J descriptor_, manifest_, aliases_, operation_;
    J semantic_catalogue_ = J::object();
    J builtin_documents_ = J::object();
    recognition::Bundle author_bundle_;
    std::set<std::string> available_images_;
    std::unique_ptr<storage::ProfileStore> profile_store_;
    std::unique_ptr<storage::WorkflowRepository> workflow_store_;
    std::unique_ptr<games::WvdQuestCatalog> catalog_;
    std::unique_ptr<runtime::NativeRunCoordinator> coordinator_;
    std::shared_ptr<devices::DeviceConnection> backend_;
    std::unique_ptr<platform::DeviceLease> preview_lease_;
    std::vector<std::uint8_t> frame_png_;
    J frame_info_ = nullptr;
    std::optional<std::chrono::steady_clock::time_point> frame_captured_at_;
    std::string active_workflow_id_, active_workflow_revision_, active_task_name_;
    std::optional<std::chrono::steady_clock::time_point> active_started_;
    std::map<std::string, std::string> active_pipeline_to_node_;
    J active_source_paths_ = J::object();
    // 序列化命令准入和设备所有权交接；重计算不占用它，停止仍可进入。
    mutable std::recursive_mutex command_mutex_;
    mutable std::mutex mutex_;
    std::atomic<bool> cancel_operation_{false};
    J submission_ = nullptr;
    std::map<std::string, J> submissions_;
    std::jthread device_worker_;
    std::jthread handoff_worker_;
    J handoff_status_ = nullptr;
    std::atomic<bool> stopping_{false};
};
} // namespace wvd::app
