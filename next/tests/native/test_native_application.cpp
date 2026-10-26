#include "workflow/serialization.hpp"
#include "app/application.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/combat/debug.hpp"
#include "games/wvd/combat/strategy.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include "devices/lifecycle_execution.hpp"
#include "submission_store_contract.hpp"
#include "publication_maintenance_contract.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <thread>
#include <fstream>
#include <future>
#include <cstdlib>
#include <new>
#include <limits>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

// Fail only the isolated repository's read buffer, after verifying its file is
// already locked. No fault switches exist in production code.
static thread_local std::size_t read_allocation_fault{};
static thread_local const wchar_t *read_fault_file{};
static thread_local bool read_fault_after_open{};
void *operator new(std::size_t size) {
    if (read_allocation_fault && size == read_allocation_fault) {
        read_allocation_fault = 0;
        const auto probe = CreateFileW(read_fault_file, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        read_fault_after_open = probe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION;
        if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
        throw std::bad_alloc();
    }
    if (auto *value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }

namespace wvd::runtime {
struct NativeCoordinatorTestAccess {
    static void start_fault(NativeRunCoordinator &coordinator, std::function<void(const char *)> fault) {
        coordinator.start_fault_ = std::move(fault);
    }
    static void image_failure(NativeRunCoordinator &coordinator, bool recorded = true) {
        storage::DiagnosticRequest request;
        request.run_id = coordinator.snapshot_.run_id; request.generation = 1;
        request.node = "isolated-image"; request.reason = "IMAGE_UNAVAILABLE";
        if (recorded) request.stage = "postcondition";
        coordinator.store_->save_diagnostic(nullptr, request);
    }
};
}

namespace wvd::platform {
struct OwnedBundleDirectoryTestAccess {
    static void fault(std::function<void(const char *)> callback) { OwnedBundleDirectory::fault_ = std::move(callback); }
};
}

namespace wvd::app {
// 仅测试替换磁盘查询依赖；生产 API 不暴露容量覆盖或离线开关。
struct ApplicationAssemblyTestAccess {
    static std::uint64_t budget(const std::filesystem::path &root, std::uint64_t retained, const nlohmann::json &cost) {
        return Application::publication_budget(root, retained, cost);
    }
    static void continue_batch(std::uint64_t retained) { Application::require_batch_continuation(retained); }

    static auto stored(Application &app) { return app.profile_store_->load(); }
    static void preparation_fault(Application &app, std::function<void(const char *)> fault) {
        app.preparation_fault_ = std::move(fault);
    }
    static void cache_fault(std::function<void(const char *, const std::filesystem::path &)> fault) {
        Application::cache_fault_ = std::move(fault);
    }
    static void preparation_ready(Application &app) { app.require_preparation_ready(); }
    static auto cleanup_status(Application &app) { std::lock_guard lock(app.mutex_); return app.preparation_cleanup_; }
    static void receipt(Application &app, const std::string &id, const nlohmann::json &intent, std::uint64_t run) {
        app.submission_store_->save(id, intent, nlohmann::json{{"request_id",id},{"state","completed"},{"run_id",run}}, false);
    }
    static void space_reader(Application &app, std::function<std::filesystem::space_info(const std::filesystem::path &)> reader) {
        app.space_query_=std::move(reader);
    }
    static void space(Application &app, std::uintmax_t bytes, bool fail = false) {
        app.space_query_ = [bytes, fail](const auto &) -> std::filesystem::space_info {
            if (fail) throw std::runtime_error("fixture-space-query");
            return {bytes, bytes, bytes};
        };
    }
    static runtime::NativeRunCoordinator &coordinator(Application &app) { return *app.coordinator_; }
    static std::shared_ptr<devices::DeviceConnection> ensure(Application &app, const nlohmann::json &stored) {
        return app.ensure_connected_for_run(stored);
    }
    static void connection(Application &app, std::shared_ptr<devices::DeviceConnection> backend,
                           std::function<void(const nlohmann::json &)> connector) {
        app.backend_ = std::move(backend); app.device_connector_ = std::move(connector);
    }
    static auto backend(Application &app) { return app.backend_; }
    static void set_backend(Application &app, std::shared_ptr<devices::DeviceConnection> backend) { app.backend_ = std::move(backend); }
    static void cancel(Application &app, bool value) { app.cancel_operation_ = value; }
    static nlohmann::json preparation_status(Application &app, nlohmann::json stored, bool enabled,
                                           storage::LogLevel level = storage::LogLevel::Info) {
        auto logging = storage::LoggingPolicy::from_profile(stored);
        logging.performance = enabled;
        logging.level = level;
        stored["logging"] = logging.json();
        {
            std::lock_guard lock(app.mutex_);
            app.operation_ = {{"state", "running"}, {"name", "start_task"}};
        }
        const auto observer = app.preparation_observer(stored);
        if (observer) {
            platform::PreparationTimer timer("compile_task_graph", observer);
            const auto first = timer.sample();
            const auto second = timer.sample();
            (void)first; (void)second;
        }
        std::lock_guard lock(app.mutex_);
        return app.operation_;
    }
    static contracts::RunSnapshot commit(Application &app, runtime::NativeRunDefinition definition,
                                          const std::shared_ptr<devices::DeviceConnection> &backend) {
        std::lock_guard lock(app.command_mutex_);
        return app.start_prepared_run(std::move(definition), backend);
    }
    static runtime::NativeRunDefinition prepare_giant(Application &app, const nlohmann::json &stored,
        const platform::PreparationObserver &observer = {}, const std::string &request_id = "p01-giant-preparation") {
        return app.assemble_task({{"task_id", "GiantBounty"}, {"resource_locale", "zh-Hant"},
            {"request_id", request_id}}, stored,
            {"offline-product", "offline-instance", "jp.co.drecom.wizardry.daphne", "", false},
            std::nullopt, false, std::nullopt, observer);
    }
    static auto giant_graph(Application &app) {
        const auto stored = app.profile_store_->load();
        return app.compile_task_graph({{"resource_locale", "zh-Hant"}}, app.catalog_->at("GiantBounty"),
            app.effective_profile_values("GiantBounty", stored), {});
    }
    static runtime::NativeRunDefinition prepare_debug(Application &app, nlohmann::json stored, bool force_selected = true) {
        auto &values = stored["values"];
        if (force_selected) {
            values["TASK_SPECIFIC_CONFIG"] = false;
            values["DEFAULT_OVERALL_STRATEGY"] = values.at("STRATEGY")[0].at("group_name");
            values["TASK_POINT_STRATEGY"]["special_combat"] = nlohmann::json::object();
        }
        const auto document = games::combat::debug_document(stored.at("revision"));
        auto library = app.workflow_store_->snapshot_closure(
            app.workflow_store_->read(games::tasks::native_public_steps.front()), games::tasks::native_public_steps);
        library[document.at("flow").at("id").get<std::string>()] = document;
        return app.assemble_workflow({{"mode", "combat_debug"}, {"resource_locale", "zh-Hant"},
            {"revision", document.at("revision")}, {"request_id", force_selected ? "combat-editor-debug" : "combat-editor-enemies"}}, stored, document,
            {"offline-product", "offline-instance", "jp.co.drecom.wizardry.daphne", "", false}, nullptr, library);
    }
    static void check_portraits(Application &app, const nlohmann::json &values) {
        const auto bundle = app.portrait_bundle(values);
        if (!bundle || bundle->files.empty()) throw std::runtime_error("CUSTOM_PORTRAIT_NOT_FROZEN");
        bundle->lease->verify_members();
    }
    static runtime::NativeRunDefinition prepare_author(Application &app, const nlohmann::json &document,
                                                       const nlohmann::json &stored) {
        const auto library = app.workflow_store_->snapshot_closure(document, games::tasks::native_public_steps);
        return app.assemble_workflow({{"revision", document.at("revision")},
            {"request_id", "critical-author-entry"}}, stored, document,
            {"offline-product", "offline-instance", "jp.co.drecom.wizardry.daphne", "", false},
            nullptr, library);
    }
    static void watch(Application &app, const std::shared_ptr<devices::DeviceConnection> &backend,
                      const std::string &id) {
        { std::lock_guard lock(app.mutex_); app.active_run_id_ = app.coordinator_->snapshot().run_id; }
        app.watch_task_session({{"task_id", "Scorpionesses"}, {"repeat", true}, {"repeat_count", 2}},
            nlohmann::json::object(), nlohmann::json::object(), backend, id, std::make_shared<std::atomic<int>>(1));
    }
    static void watch_budget(Application &app, const std::shared_ptr<devices::DeviceConnection> &backend,
        const std::string &id, const nlohmann::json &stored) {
        { std::lock_guard lock(app.mutex_); app.active_run_id_ = app.coordinator_->snapshot().run_id; }
        app.watch_task_session({{"task_id","GiantBounty"},{"resource_locale","zh-Hant"},
            {"repeat",true},{"repeat_count",2}}, stored, app.effective_profile_values("GiantBounty",stored),
            backend,id,std::make_shared<std::atomic<int>>(1));
    }
    static nlohmann::json queue(Application &app, const std::string &id, int &prepared) {
        return app.queue_run("closure", {{"request_id", id}}, {{"fixture", "closure"}}, [&prepared] {
            ++prepared; return nlohmann::json{{"run_id", 0}};
        }, app.stop_epoch_.load());
    }
};
}

namespace {
using J = nlohmann::json;
using namespace std::chrono_literals;

class OfflineConnection final : public wvd::devices::DeviceConnection,
                                public wvd::devices::LifecyclePort {
  public:
    explicit OfflineConnection(const std::filesystem::path &pack) {
        auto marker = cv::imread((pack / "image/Inn.png").string(), cv::IMREAD_COLOR);
        if (marker.empty() || marker.cols > 900 || marker.rows > 1600)
            throw std::runtime_error("OFFLINE_MARKER_INVALID");
        cv::Mat frame(1600, 900, CV_8UC3, cv::Scalar(0, 0, 0));
        marker.copyTo(frame(cv::Rect(60, 450, marker.cols, marker.rows)));
        pixels_.assign(frame.data, frame.data + frame.total() * frame.elemSize());
    }
    bool offline() const override { return true; }
    bool verified_access() const override { return true; }
    bool connect() override { ++connections; if (on_connect) on_connect(); return true; }
    std::function<void()> on_connect;
    std::atomic<unsigned> connections{0}, inputs{0}, captures{0};
    wvd::devices::RawFrame capture() override {
        ++captures;
        wvd::devices::RawFrame frame;
        frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(pixels_);
        frame.size = {900, 1600};
        frame.device_id = "offline-product";
        frame.viewport_id = "900x1600";
        frame.foreground_application = "jp.co.drecom.wizardry.daphne";
        frame.captured_at = std::chrono::steady_clock::now();
        frame.capture_finished_at = frame.captured_at;
        frame.connection_generation = 1;
        frame.display_rotation = 0;
        return frame;
    }
    bool execute(const wvd::contracts::Command &) override {
        ++inputs;
        throw std::runtime_error("OFFLINE_BUSINESS_INPUT_FORBIDDEN");
    }
    wvd::devices::LifecyclePort *lifecycle_port() override { return this; }
    wvd::devices::LifecycleTarget lifecycle_target() const override {
        return {"offline-product", "offline-instance", "jp.co.drecom.wizardry.daphne", "", false};
    }
    bool matches_selection(const std::filesystem::path &, int, const std::string &) const override {
        return true;
    }
    void set_vpn_required(bool value) override {
        if (value) throw std::runtime_error("OFFLINE_VPN_NOT_SUPPORTED");
    }
    J diagnostics() const override { return J::array(); }
    std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
        return wvd::devices::LifecycleObservation{lifecycle_target(), true, true, true, true,
            1, std::chrono::steady_clock::now(), true};
    }
    bool execute_lifecycle(wvd::devices::LifecycleOperation,
                           const wvd::devices::LifecycleTarget &,
                           const std::function<bool()> &) override {
        throw std::runtime_error("OFFLINE_LIFECYCLE_ACTION_FORBIDDEN");
    }
  private:
    std::vector<std::uint8_t> pixels_;
};
class BindingConnection final : public wvd::devices::DeviceConnection, public wvd::devices::LifecyclePort {
  public:
    wvd::devices::LifecycleTarget target{"127.0.0.1:16448", "2", "jp.co.drecom.wizardry.daphne", "vpn", true};
    bool exited{}, running{true}, connected{true}, game{}, vpn_ready{}, cleanup_fail{}, binding_matches{true}, probe_fail{}, stale{}, mismatch{};
    int disconnected{}, observed{}, actions{};
    int bounded_reads{}, cleared_reads{};
    std::chrono::steady_clock::time_point read_deadline{};
    void observation_window(std::chrono::steady_clock::time_point deadline, std::stop_token) override {
        read_deadline = deadline;
        if (deadline == std::chrono::steady_clock::time_point{}) ++cleared_reads;
        else ++bounded_reads;
    }
    std::function<void()> after_observation;
    bool offline() const override { return false; }
    bool verified_access() const override { return true; }
    bool connect() override { return true; }
    bool execute(const wvd::contracts::Command &) override { throw std::runtime_error("FIXTURE_INPUT_FORBIDDEN"); }
    wvd::devices::RawFrame capture() override { throw std::runtime_error("FIXTURE_CAPTURE_FORBIDDEN"); }
    wvd::devices::LifecycleTarget lifecycle_target() const override { return target; }
    bool matches_selection(const std::filesystem::path &, int, const std::string &) const override { return binding_matches; }
    void set_vpn_required(bool value) override { target.vpn_required = value; }
    J diagnostics() const override { return J::object(); }
    void disconnect() override {
        ++disconnected;
        if (cleanup_fail) throw std::runtime_error("DEVICE_CLEANUP_PENDING");
        connected = false;
    }
    wvd::devices::LifecyclePort *lifecycle_port() override { return this; }
    std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
        ++observed;
        if (probe_fail) throw std::runtime_error("FIXTURE_OBSERVATION_FAILED");
        auto identity = target; if (mismatch) identity.instance_id = "another-instance";
        auto at = std::chrono::steady_clock::now(); if (stale) at -= 10s;
        if (after_observation) after_observation();
        return wvd::devices::LifecycleObservation{identity, running, connected, game, vpn_ready, 1, at, game, exited};
    }
    bool execute_lifecycle(wvd::devices::LifecycleOperation operation,
        const wvd::devices::LifecycleTarget &identity, const std::function<bool()> &cancelled) override {
        if (identity.device_id != target.device_id || identity.instance_id != target.instance_id)
            throw std::runtime_error("FIXTURE_BINDING_CHANGED");
        if (cancelled()) return false;
        if (operation == wvd::devices::LifecycleOperation::RestartInstance)
            throw std::runtime_error("INITIAL_RESTART_FORBIDDEN");
        if (operation == wvd::devices::LifecycleOperation::EnsureVpn) vpn_ready = true;
        if (operation == wvd::devices::LifecycleOperation::StartApplication) game = true;
        ++actions; return true;
    }
};

J call(wvd::app::Application &application, wvd::api::http::verb method,
       const std::string &path, const J &body = J::object()) {
    wvd::api::Request request{method, path, 11};
    if (method != wvd::api::http::verb::get) {
        request.body() = body.dump();
        request.prepare_payload();
    }
    const auto reply = application.handle(request);
    const auto value = J::parse(reply.body);
    if (static_cast<unsigned>(reply.status) >= 400)
        throw std::runtime_error("PRODUCT_API_" + path + ":" + value.dump());
    return value;
}

void require_closure(bool value, const std::string &code) { if (!value) throw std::runtime_error(code); }
class CompletedCycle final : public wvd::contracts::BusinessRunState {
    void on_segment(wvd::contracts::SegmentBoundary, std::uint64_t, std::size_t) override {}
    J summarize() const override {
        return {{"bounty_cycle", {{"completed_cycles", 1}, {"reports_remaining", 0}}},
            {"bounty_report_pending", false}, {"inn_payment_pending", false}};
    }
};
wvd::runtime::NativeRunDefinition closure_definition(const std::filesystem::path &root, const std::string &id,
    std::function<void()> before_operations = {}) {
    using namespace wvd;
    std::filesystem::create_directories(root);
    { std::ofstream marker(root / "marker.txt"); marker << "closure-only"; }
    recognition::Bundle bundle{root, "closure", {{"marker.txt", platform::file_sha256(root / "marker.txt")}}};
    workflow::FlowProgram program;
    program.revision = "pending"; program.root_definition = "root";
    workflow::Definition flow; flow.id = "root"; flow.entry = "checkpoint";
    const auto source = J::array({{{"flow_id", "closure"}, {"node_id", "checkpoint"}}}).dump();
    workflow::Step checkpoint; checkpoint.id = "checkpoint"; checkpoint.source_path = source;
    checkpoint.data = workflow::RegisteredOperation{"BusinessCheckpoint", J::object()}; checkpoint.next = {"finish"};
    workflow::Step finish; finish.id = "finish"; finish.source_path = J::array({{{"flow_id", "closure"}, {"node_id", "finish"}}}).dump(); finish.data = workflow::Finish{};
    flow.steps.emplace(checkpoint.id, checkpoint); flow.steps.emplace(finish.id, finish);
    program.definitions.emplace(flow.id, std::move(flow));
    const J identity{{"engine_kind", "wvd_native"}, {"program_schema", workflow::FlowProgram::schema},
        {"source_revision", "closure-source"}, {"source_files", {{"marker.txt", bundle.files.front().sha256}}},
        {"mod_revision", nullptr}, {"mod_files", J::object()}, {"image_sources", J::object()},
        {"aliases", J::object()}, {"authoring", J::object()}, {"definition", "closure"},
        {"program", workflow::serialize(program)}};
    const auto material = identity.dump();
    program.revision = bundle.revision = platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(material.data()), material.size()});
    std::filesystem::create_directories(root / "program");
    { std::ofstream output(root / "program/identity.json"); output << identity.dump(2); }
    { std::ofstream output(root / "program/flow.json"); output << workflow::serialize(program).dump(2); }
    bundle.files.push_back({"program/identity.json", platform::file_sha256(root / "program/identity.json")});
    bundle.files.push_back({"program/flow.json", platform::file_sha256(root / "program/flow.json")});
    runtime::NativeRunDefinition result; result.request_id = id;
    result.policy.device_id = "offline-product"; result.policy.game_id = "wvd";
    result.policy.application_id = "jp.co.drecom.wizardry.daphne";
    result.policy.pack_revision = program.revision; result.policy.viewport_id = "900x1600";
    result.policy.allowed_scenes.insert("wvd");
    const auto frozen = std::make_shared<const workflow::FlowProgram>(std::move(program));
    for (int n = 0; n < 3; ++n) result.units.push_back({frozen, bundle, {}, source, 5s});
    result.total_time_limit = 15s;
    result.create_state = [](const auto &) { return std::make_unique<CompletedCycle>(); };
    result.operations = [before_operations](auto &, auto, auto mark) {
        if (before_operations) before_operations();
        return [mark](runtime::NativeFlowPorts &) {
            return [mark](const std::string &binding, const auto &, const auto &, const auto &, const std::string &path) {
                if (binding != "BusinessCheckpoint") throw std::runtime_error("CLOSURE_UNEXPECTED_OPERATION");
                mark(path); return runtime::OperationResult{runtime::OperationState::Done};
            };
        };
    };
    return result;
}
std::vector<std::uint64_t> ownership_file_id(const std::filesystem::path &path) {
    const auto h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require_closure(h != INVALID_HANDLE_VALUE, "OWNERSHIP_TEST_OPEN_FAILED");
    BY_HANDLE_FILE_INFORMATION info{}; const auto ok = GetFileInformationByHandle(h, &info); CloseHandle(h);
    require_closure(ok != FALSE, "OWNERSHIP_TEST_IDENTITY_FAILED");
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
void require_delete_share_denial(const std::filesystem::path &path) {
    const auto probe = CreateFileW(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const auto error = GetLastError();
    if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
    require_closure(probe == INVALID_HANDLE_VALUE && error == ERROR_SHARING_VIOLATION,
        "OWNERSHIP_TEST_DELETE_NOT_ACTUALLY_DENIED:" + std::to_string(error));
}
__declspec(noinline) int preparation_ownership_contract(const std::filesystem::path &pack,
    const std::filesystem::path &quests, const std::filesystem::path &root) {
    using A = wvd::app::ApplicationAssemblyTestAccess;
    using C = wvd::runtime::NativeCoordinatorTestAccess;
    using Owner = wvd::platform::OwnedBundleDirectory;
    require_closure(!std::filesystem::exists(root), "PREPARATION_OWNERSHIP_ROOT_MUST_BE_FRESH");
    std::filesystem::create_directories(root);
    J proof = J::array();
    {
        const auto data = root / "application";
        auto backend = std::make_shared<OfflineConnection>(pack);
        wvd::app::Application app({data, pack, {}, quests}, backend);
        auto &coordinator = A::coordinator(app);
        const auto published = data / "published"; std::filesystem::create_directories(published);
        const auto history = published / "historical-completed";
        auto history_owner = Owner::create(history);
        auto history_definition = closure_definition(history, "historical-completed");
        const auto history_receipt = history_owner->receipt();
        history_definition.publication_ownership = std::move(history_owner);
        A::commit(app, std::move(history_definition), backend);
        require_closure(coordinator.wait_for_worker(5s) && coordinator.collect_finished_worker(), "HISTORY_WORKER_NOT_RELEASED");
        require_closure(history_receipt->accepted && coordinator.snapshot().state == wvd::contracts::RunState::Completed,
            "HISTORY_NOT_ACTUALLY_ADOPTED_AND_COMPLETED");
        const auto old_hash = wvd::platform::file_sha256(history / "marker.txt");
        const auto old_id = ownership_file_id(history / "marker.txt");
        const auto historical_unchanged = [&] {
            require_closure(wvd::platform::file_sha256(history / "marker.txt") == old_hash &&
                ownership_file_id(history / "marker.txt") == old_id, "PREPARATION_CHANGED_HISTORICAL_PACKAGE");
        };
        const auto stored = A::stored(app);
        const auto original_captures = backend->captures.load(), original_connections = backend->connections.load();
        for (const std::string phase : {"task.after_publication", "task.before_units", "workflow.after_publication", "workflow.before_units"}) {
            bool reached{};
            A::preparation_fault(app, [&](const char *at) {
                if (phase == at) { reached = true; throw std::runtime_error("ASSEMBLY_FAULT:" + phase); }
            });
            bool failed{}; std::string actual_error;
            try {
                if (phase.starts_with("task.")) { auto prepared = A::prepare_giant(app, stored); }
                else { auto prepared = A::prepare_debug(app, stored); }
            } catch (const std::exception &e) { actual_error = e.what(); failed = actual_error == "ASSEMBLY_FAULT:" + phase; }
            A::preparation_fault(app, {});
            const auto target = published / (phase.starts_with("task.") ? "p01-giant-preparation" : "combat-editor-debug");
            require_closure(reached && failed && !std::filesystem::exists(target), "ASSEMBLY_FAILURE_LEFT_PUBLICATION:" + phase +
                ";hook_reached=" + std::to_string(reached) + ";target_exists=" + std::to_string(std::filesystem::exists(target)) +
                ";actual_error=" + actual_error);
            require_closure(backend->inputs == 0 && backend->captures == original_captures && backend->connections == original_connections,
                "ASSEMBLY_FAILURE_REACHED_DEVICE:" + phase);
            historical_unchanged();
            proof.push_back({{"case", phase}, {"publication_removed", true}, {"new_inputs", 0}, {"history_unchanged", true}});
        }
        for (const std::string phase : {"before_coordinator", "before_state_publication", "before_worker"}) {
            const auto target = published / ("unaccepted-" + phase);
            auto owner = Owner::create(target); const auto receipt = owner->receipt();
            auto definition = closure_definition(target, "unaccepted-" + phase);
            definition.publication_ownership = std::move(owner);
            bool reached{};
            const auto inject = [&](const char *at) {
                if (phase == at) { reached = true; throw std::runtime_error("BEFORE_ADOPTION_FAULT:" + phase); }
            };
            if (phase == "before_coordinator") A::preparation_fault(app, inject);
            else C::start_fault(coordinator, inject);
            bool failed{};
            try { A::commit(app, std::move(definition), backend); }
            catch (const std::exception &e) { failed = std::string(e.what()) == "BEFORE_ADOPTION_FAULT:" + phase; }
            A::preparation_fault(app, {}); C::start_fault(coordinator, {});
            require_closure(reached && failed && !receipt->accepted && !receipt->cleanup_pending &&
                !std::filesystem::exists(target), "PRE_ADOPTION_FAILURE_DID_NOT_WITHDRAW:" + phase);
            require_closure(backend->inputs == 0 && backend->captures == original_captures && backend->connections == original_connections,
                "PRE_ADOPTION_FAILURE_STARTED_WORKER:" + phase);
            historical_unchanged();
            proof.push_back({{"case", phase}, {"accepted", receipt->accepted}, {"publication_removed", true}, {"new_inputs", 0}});
        }
        {
            const auto target = published / "accepted-snapshot-error";
            auto owner = Owner::create(target); const auto receipt = owner->receipt();
            auto definition = closure_definition(target, "accepted-snapshot-error");
            definition.publication_ownership = std::move(owner);
            bool worker_completed{};
            C::start_fault(coordinator, [&](const char *at) {
                if (std::string(at) != "after_adoption") return;
                worker_completed = coordinator.wait_for_worker(5s) &&
                    coordinator.snapshot().state == wvd::contracts::RunState::Completed;
                throw std::runtime_error("AFTER_ADOPTION_SNAPSHOT_FAULT");
            });
            bool failed{};
            try { A::commit(app, std::move(definition), backend); }
            catch (const std::exception &e) { failed = std::string(e.what()) == "AFTER_ADOPTION_SNAPSHOT_FAULT"; }
            C::start_fault(coordinator, {});
            require_closure(coordinator.collect_finished_worker(), "ACCEPTED_WORKER_NOT_COLLECTED_AFTER_START_THROW");
            require_closure(failed && worker_completed && receipt->accepted && std::filesystem::exists(target / "marker.txt") &&
                !receipt->cleanup_attempted && coordinator.snapshot().quiescent, "ACCEPTED_COMPLETED_PACKAGE_WITHDRAWN");
            historical_unchanged();
            proof.push_back({{"case", "accepted-fast-worker-snapshot-error"}, {"accepted", true},
                {"worker_completed", worker_completed}, {"package_retained", true}, {"quiescent", true}});
        }
        {
            HANDLE held = INVALID_HANDLE_VALUE;
            const auto target = published / "p01-giant-preparation";
            A::preparation_fault(app, [&](const char *at) {
                if (std::string(at) != "task.after_publication") return;
                held = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                require_closure(held != INVALID_HANDLE_VALUE, "ASSEMBLY_CLEANUP_LOCK_FAILED");
                require_delete_share_denial(target);
                throw std::runtime_error("ASSEMBLY_PRIMARY_FAILURE");
            });
            std::string primary, cleanup;
            try { auto prepared = A::prepare_giant(app, stored); }
            catch (const wvd::platform::DirectoryCleanupPending &e) { primary = e.primary_error(); cleanup = e.cleanup_error(); }
            A::preparation_fault(app, {});
            if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
            require_closure(primary.find("ASSEMBLY_PRIMARY_FAILURE") != std::string::npos && !cleanup.empty() &&
                std::filesystem::exists(target), "ASSEMBLY_PRIMARY_OR_CLEANUP_ERROR_LOST");
            const auto before = submission_contract::entry_count(published);
            submission_contract::rejects([&] { auto prepared = A::prepare_giant(app, stored, {},
                "p01-distinct-request-after-cleanup-failure"); }, "PREPARATION_CLEANUP_PENDING");
            require_closure(submission_contract::entry_count(published) == before, "CLEANUP_PENDING_RETRY_CREATED_NEW_UUID");
            const auto status = A::cleanup_status(app);
            require_closure(status.at("primary_error").get<std::string>().find("ASSEMBLY_PRIMARY_FAILURE") != std::string::npos &&
                !status.at("cleanup_error").get<std::string>().empty(), "ASSEMBLY_CLEANUP_NOT_DIAGNOSABLE");
            historical_unchanged();
            proof.push_back({{"case", "assembly-primary-plus-real-cleanup-share-denial"}, {"primary_error", primary},
                {"cleanup_error", cleanup}, {"retry_blocked", true}, {"status", status}});
        }
    }
    {
        using O = wvd::platform::OwnedBundleDirectoryTestAccess;
        const auto data = root / "accepted-index-application";
        const auto published = data / "published";
        const auto target = published / "accepted-index-removal-failure";
        std::shared_ptr<const Owner::Receipt> accepted_receipt;
        std::string primary, cleanup;
        std::uint64_t completed_run{};
        std::string retained_hash;
        std::vector<std::uint64_t> retained_id;
        const auto assert_blocked_api = [](wvd::app::Application &app, const std::string &id) {
            bool blocked{};
            try { call(app, wvd::api::http::verb::post, "/api/v1/runs/start",
                {{"request_id", id}, {"task_id", "GiantBounty"}, {"resource_locale", "zh-Hant"}}); }
            catch (const std::exception &error) {
                blocked = std::string(error.what()).find("PREPARATION_CLEANUP_PENDING") != std::string::npos;
            }
            require_closure(blocked, "ACCEPTED_INDEX_NEW_UUID_NOT_REJECTED_BY_REAL_API");
        };
        {
            auto backend = std::make_shared<OfflineConnection>(pack);
            wvd::app::Application app({data, pack, {}, quests}, backend);
            auto &coordinator = A::coordinator(app);
            std::filesystem::create_directories(published);
            auto owner = Owner::create(published / ("accepted-index.staging-" + wvd::platform::unique_id()));
            owner->rename_to(target);
            accepted_receipt = owner->receipt();
            auto definition = closure_definition(target, "accepted-index-removal-failure");
            definition.publication_ownership = std::move(owner);
            O::fault([](const char *phase) {
                if (std::string(phase) == "before_prepare_marker_remove")
                    throw std::runtime_error("ACCEPTED_INDEX_REMOVE_INJECTED");
                if (std::string(phase) == "before_cleanup_marker") throw std::bad_alloc();
            });
            try { A::commit(app, std::move(definition), backend); }
            catch (const wvd::platform::DirectoryCleanupPending &error) {
                primary = error.primary_error(); cleanup = error.cleanup_error();
            }
            O::fault({});
            require_closure(primary.find("OWNED_DIRECTORY_ACCEPT_INDEX_FAILED") != std::string::npos &&
                cleanup.find("ACCEPTED_INDEX_REMOVE_INJECTED") != std::string::npos &&
                accepted_receipt->accepted && accepted_receipt->cleanup_pending && accepted_receipt->marker_failed,
                "ACCEPTED_INDEX_ERROR_OR_ADOPTION_STATE_LOST:" + primary + ";cleanup=" + cleanup);
            require_closure(coordinator.wait_for_worker(5s) && coordinator.collect_finished_worker() &&
                coordinator.snapshot().state == wvd::contracts::RunState::Completed && coordinator.snapshot().quiescent,
                "ACCEPTED_INDEX_ERROR_INTERRUPTED_WORKER");
            completed_run = coordinator.snapshot().run_id;
            require_closure(std::filesystem::exists(target / "marker.txt") && !accepted_receipt->cleanup_attempted,
                "ACCEPTED_INDEX_ERROR_WITHDREW_ACCEPTED_PACKAGE");
            retained_hash = wvd::platform::file_sha256(target / "marker.txt");
            retained_id = ownership_file_id(target / "marker.txt");
            const auto before = submission_contract::entry_count(published);
            const auto captures = backend->captures.load(), connections = backend->connections.load();
            assert_blocked_api(app, "accepted-index-distinct-next");
            require_closure(submission_contract::entry_count(published) == before &&
                coordinator.snapshot().run_id == completed_run && backend->inputs == 0 && backend->captures == captures &&
                backend->connections == connections, "ACCEPTED_INDEX_ERROR_NEW_REQUEST_GREW_OR_TOUCHED_DEVICE");
            const auto status = A::cleanup_status(app);
            require_closure(status.at("primary_error").get<std::string>().find("OWNED_DIRECTORY_ACCEPT_INDEX_FAILED") != std::string::npos &&
                status.at("cleanup_error").get<std::string>().find("ACCEPTED_INDEX_REMOVE_INJECTED") != std::string::npos,
                "ACCEPTED_INDEX_APPLICATION_DIAGNOSIS_INACCURATE");
        }
        require_closure(accepted_receipt->accepted && std::filesystem::exists(target / "marker.txt"),
            "ACCEPTED_INDEX_OWNER_DESTRUCTION_REMOVED_HISTORY");
        {
            auto backend = std::make_shared<OfflineConnection>(pack);
            wvd::app::Application reopened({data, pack, {}, quests}, backend);
            const auto before = submission_contract::entry_count(published);
            assert_blocked_api(reopened, "accepted-index-after-restart");
            require_closure(submission_contract::entry_count(published) == before &&
                wvd::platform::file_sha256(target / "marker.txt") == retained_hash &&
                ownership_file_id(target / "marker.txt") == retained_id && backend->inputs == 0 &&
                backend->captures == 0 && backend->connections == 0,
                "ACCEPTED_INDEX_RESTART_BYPASSED_DURABLE_BLOCK_OR_CHANGED_HISTORY");
        }
        proof.push_back({{"case", "accepted-index-removal-plus-marker-allocation-failure"}, {"accepted", true},
            {"primary_error", primary}, {"cleanup_error", cleanup}, {"worker_completed", true},
            {"run_id", completed_run}, {"package_retained_after_owner_destroy", true}, {"new_uuid_blocked_after_restart", true}});
    }
    for (const std::string phase : {"cache.before_copy", "cache.before_hash", "cache.before_rename"}) {
        const auto case_root = root / phase.substr(6);
        const auto small_pack = case_root / "pack"; std::filesystem::create_directories(small_pack / "parameters");
        std::filesystem::copy_file(pack / "parameters/legacy-config-fields.json", small_pack / "parameters/legacy-config-fields.json");
        { std::ofstream file(small_pack / "asset.bin", std::ios::binary); file << "isolated-cache-source"; }
        { std::ofstream file(small_pack / "manifest.json"); file << J{{"revision", "cache-fixture"}, {"files", J::array({
            {{"path", "asset.bin"}, {"sha256", wvd::platform::file_sha256(small_pack / "asset.bin")}}})}}.dump(); }
        const auto source_sha = wvd::platform::file_sha256(small_pack / "asset.bin");
        const auto data = case_root / "data";
        std::filesystem::path staging, collision; HANDLE held = INVALID_HANDLE_VALUE; bool reached{};
        A::cache_fault([&](const char *at, const std::filesystem::path &path) {
            if (std::string(at) == "cache.after_create") staging = path;
            if (phase != at) return;
            reached = true;
            held = CreateFileW(staging.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            require_closure(held != INVALID_HANDLE_VALUE, "CACHE_CLEANUP_LOCK_FAILED");
            require_delete_share_denial(staging);
            if (phase == "cache.before_copy") std::filesystem::create_directory(path);
            if (phase == "cache.before_hash") { std::ofstream file(path, std::ios::binary); file << "wrong-cache-copy"; }
            if (phase == "cache.before_rename") {
                collision = path; std::filesystem::create_directory(collision);
                std::ofstream marker(collision / "external-marker"); marker << "must-preserve";
            }
        });
        std::string primary, cleanup;
        try { wvd::app::Application failed({data, small_pack, {}, quests}, std::make_shared<OfflineConnection>(pack)); }
        catch (const wvd::platform::DirectoryCleanupPending &e) { primary = e.primary_error(); cleanup = e.cleanup_error(); }
        A::cache_fault({}); if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
        require_closure(reached && !primary.empty() && !cleanup.empty() && std::filesystem::exists(staging),
            "CACHE_FAILURE_LOST_PRIMARY_CLEANUP_OR_STAGING:" + phase);
        const auto cache_root = data / "asset-cache"; const auto before = submission_contract::entry_count(cache_root);
        submission_contract::rejects([&] { wvd::app::Application retried({data, small_pack, {}, quests},
            std::make_shared<OfflineConnection>(pack)); }, "PREPARATION_CLEANUP_PENDING");
        require_closure(submission_contract::entry_count(cache_root) == before &&
            wvd::platform::file_sha256(small_pack / "asset.bin") == source_sha, "CACHE_FAILURE_RETRY_GREW_OR_CHANGED_SOURCE");
        if (!collision.empty()) require_closure(submission_contract::read_text(collision / "external-marker") == "must-preserve",
            "CACHE_RENAME_FAILURE_OVERWROTE_DESTINATION");
        proof.push_back({{"case", phase}, {"primary_error", primary}, {"cleanup_error", cleanup},
            {"staging", staging.string()}, {"retry_blocked", true}, {"source_unchanged", true}});
    }
    std::ofstream(root / "results.json") << proof.dump(2);
    std::cout << "PASS actual Application/Coordinator: assembly ownership, pre-adoption rollback, accepted fast worker retained, cache dual-error barriers\n";
    return 0;
}
int resource_application_batch(const std::filesystem::path &pack, const std::filesystem::path &quests,
    const std::filesystem::path &root) {
    using namespace wvd;
    using A = app::ApplicationAssemblyTestAccess;
    using C = runtime::NativeCoordinatorTestAccess;
    require_closure(!std::filesystem::exists(root), "RESOURCE_APP_BATCH_FRESH_ROOT_REQUIRED");
    std::filesystem::create_directories(root);
    auto backend = std::make_shared<OfflineConnection>(pack);
    const auto data = root / "data";
    app::Application application({data,pack,{},quests},backend);
    auto stored = A::stored(application);
    stored["values"]["AUTO_START_CLASH"] = false;
    stored["values"]["ACTIVE_BEG_MONEY"] = false;
    auto logging = storage::LoggingPolicy::from_profile(stored);
    logging.memory = false; logging.performance = false; stored["logging"] = logging.json();
    auto first = A::prepare_giant(application,stored,{},"batch-first");
    auto resources = first.units.front().bundle;
    auto identity = J::parse(resources.lease->bytes("program/identity.json"));
    const auto digest = resources.lease->resource_digest();
    const auto resource_bytes = resources.lease->resource_bytes();
    const auto model_path = resources.lease->physical_member("model/ocr/det.onnx");
    const auto model_id = ownership_file_id(model_path);
    const auto links = [&](const std::filesystem::path &path) {
        const auto handle = CreateFileW(platform::extended_path(path).c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        require_closure(handle != INVALID_HANDLE_VALUE,"BATCH_LINKS_OPEN_FAILED");
        BY_HANDLE_FILE_INFORMATION info{};const auto ok=GetFileInformationByHandle(handle,&info);CloseHandle(handle);
        require_closure(ok != FALSE,"BATCH_LINKS_READ_FAILED");return info.nNumberOfLinks;
    };
    const auto model_links = links(model_path);
    // The external business is the pre-existing three-checkpoint fixture.
    // Publisher, layout binding, Application scheduling, RunStore and joins stay real.
    auto external = closure_definition(root / "external-business-fixture","external");
    auto program = *external.units.front().program; program.revision="pending";
    identity["program"] = workflow::serialize(program);
    identity["authoring"] = J::object(); identity["definition"]="isolated-business-budget";
    const auto material=identity.dump();
    program.revision=resources.revision=platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(material.data()),material.size()});
    first.units.clear();resources.lease.reset(); // No metadata is rewritten while leased.
    const auto write_json = [&](const std::string &name,const J &value) {
        std::ofstream output(platform::extended_path(resources.root / platform::BundleLease::checked_relative(name)),std::ios::binary|std::ios::trunc);
        output << value.dump(2);output.close();require_closure(bool(output),"BATCH_FIXTURE_WRITE_FAILED");
    };
    write_json("program/identity.json",identity);write_json("program/flow.json",workflow::serialize(program));
    { std::ifstream input(platform::extended_path(resources.root / "publication-layout.json"));
      auto layout=J::parse(input);input.close();layout["program_revision"]=program.revision;write_json("publication-layout.json",layout); }
    for(auto &file:resources.files) if(file.relative_path=="program/identity.json"||file.relative_path=="program/flow.json")
        file.sha256=platform::file_sha256(resources.root / platform::BundleLease::checked_relative(file.relative_path));
    resources.lease=platform::BundleLease::open_publication(resources.root);
    require_closure(resources.lease->revision()==program.revision && resources.lease->resource_digest()==digest,
        "BATCH_REBOUND_FIXTURE_REJECTED_OR_CHANGED_RESOURCES");
    const auto frozen=std::make_shared<const workflow::FlowProgram>(std::move(program));
    first.units.assign(3,{frozen,resources,{},external.units.front().checkpoint_source_path,5s});
    first.policy.pack_revision=resources.revision;first.startup.reset();first.total_time_limit=15s;
    first.create_state=std::move(external.create_state);first.operations=std::move(external.operations);
    first.recovery={};first.handoff_ready={};first.logging=logging;
    auto &coordinator=A::coordinator(application);
    const auto started=A::commit(application,std::move(first),backend);resources.lease.reset();
    require_closure(coordinator.wait_for(10s),"BATCH_FIRST_NOT_QUIESCENT");
    const auto terminal=coordinator.snapshot();
    require_closure(terminal.state==contracts::RunState::Completed && terminal.completed_business_units==3 &&
        terminal.result_saved && terminal.quiescent && terminal.storage_error.empty() && terminal.business.at("bounty_cycle").at("completed_cycles")==1 &&
        !terminal.business.value("bounty_report_pending",true) && !terminal.business.value("inn_payment_pending",true),"BATCH_FIRST_NOT_CLEAN");
    std::atomic<std::uint64_t> second_adopted{};
    C::start_fault(coordinator,[&](const char *phase) {
        if(std::string_view(phase)=="after_adoption") {
            second_adopted=coordinator.snapshot().run_id;coordinator.request_stop();
        }
    });
    struct ResetHook {runtime::NativeRunCoordinator &coordinator;~ResetHook(){C::start_fault(coordinator,{});}} reset{coordinator};
    const auto tree_bytes = [](const std::filesystem::path &path) {
        std::uint64_t size=0;if(!std::filesystem::exists(platform::extended_path(path)))return size;
        for(const auto &entry:std::filesystem::recursive_directory_iterator(platform::extended_path(path))) {
            require_closure(!(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT),"BATCH_TEST_REPARSE");
            if(entry.is_regular_file())size+=entry.file_size();
        }return size;
    };
    const auto cache_bytes = [&] {
        std::uint64_t bytes=0;for(const auto &path:{data/"published/.asset-sets",data/"published/asset-objects",data/"asset-cache",data/"active-snapshots"})bytes+=tree_bytes(path);return bytes;
    };
    A::watch_budget(application,backend,"batch-first",stored);
    J before;
    const auto deadline=std::chrono::steady_clock::now()+90s;
    while(std::chrono::steady_clock::now()<deadline) {
        before=call(application,api::http::verb::get,"/api/v1/runs/current");
        if(before.at("repeat").value("state","")=="waiting")break;
        std::this_thread::sleep_for(20ms);
    }
    const auto first_view=coordinator.read_view();
    require_closure(before.at("repeat").at("state")=="waiting" && before.at("repeat").at("completed_cycles")==1 &&
        first_view.request_id=="batch-first" && first_view.snapshot.run_id==started.run_id && first_view.worker_joined,
        "BATCH_FIRST_WAITING_NOT_BOUND_TO_SOURCE");
    const auto first_run=first_view.store->directory();
    const auto stable_first_run=tree_bytes(first_run), before_cache=cache_bytes();
    J after;
    while(std::chrono::steady_clock::now()<deadline) {
        after=call(application,api::http::verb::get,"/api/v1/runs/current");
        if(!after.at("repeat").value("active",true))break;
        std::this_thread::sleep_for(20ms);
    }
    const auto second_view=coordinator.read_view();
    require_closure(second_adopted!=0 && second_view.snapshot.run_id==second_adopted && second_view.snapshot.run_id!=started.run_id &&
        second_view.request_id!="batch-first" && second_view.snapshot.state==contracts::RunState::UserStopped &&
        second_view.snapshot.result_saved && second_view.snapshot.quiescent && second_view.snapshot.storage_error.empty() && second_view.worker_joined &&
        !after.at("repeat").value("active",true) && after.at("repeat").at("completed_cycles")==1,
        "BATCH_SECOND_NOT_ACTUAL_ADOPTED_STOPPED_JOINED_RUN:"+after.dump());
    const auto second_root=data/"published"/second_view.request_id;
    const auto second=platform::BundleLease::open_publication(second_root);
    require_closure(second->resource_digest()==digest && second->resource_bytes()==resource_bytes &&
        ownership_file_id(second->physical_member("model/ocr/det.onnx"))==model_id && links(model_path)==model_links,
        "BATCH_RESOURCE_DIGEST_FILEID_OR_LINKS_CHANGED");
    const auto after_cache=cache_bytes();
    require_closure(after.at("repeat").at("retained_reference_bytes").get<std::uint64_t>()>
        before.at("repeat").at("retained_reference_bytes").get<std::uint64_t>(),"BATCH_LOGICAL_REFERENCE_DID_NOT_GROW");
    require_closure(after.at("repeat").at("retained_budget_bytes").get<std::uint64_t>()>
        before.at("repeat").at("retained_budget_bytes").get<std::uint64_t>(),"BATCH_RETAINED_BUDGET_DID_NOT_GROW");
    const auto ref_delta=after.at("repeat").at("retained_reference_bytes").get<std::uint64_t>()-before.at("repeat").at("retained_reference_bytes").get<std::uint64_t>();
    const auto budget_delta=after.at("repeat").at("retained_budget_bytes").get<std::uint64_t>()-before.at("repeat").at("retained_budget_bytes").get<std::uint64_t>();
    const auto marker=std::filesystem::file_size(second_root/"publication-layout.json");
    require_closure(after_cache==before_cache && tree_bytes(first_run)==stable_first_run && ref_delta>budget_delta &&
        ref_delta-budget_delta==resource_bytes-marker,"BATCH_LOGICAL_AND_RETAINED_GROWTH_NOT_DISTINCT");
    const auto stable_second=tree_bytes(second_view.store->directory());std::this_thread::sleep_for(100ms);
    require_closure(stable_second==tree_bytes(second_view.store->directory()),"BATCH_TERMINAL_FACTS_STILL_WRITING");
    std::ofstream(root/"result.json")<<J{{"passed",true},{"first_business","existing external three-checkpoint fixture"},
        {"first",before},{"second",after},{"first_run_id",started.run_id},{"second_run_id",second_view.snapshot.run_id},
        {"second_request_id",second_view.request_id},{"second_state","UserStopped"},{"completed_cycles",1},
        {"resource_digest",digest},{"resource_bytes",resource_bytes},{"reference_delta",ref_delta},{"budget_delta",budget_delta},
        {"new_marker_bytes",marker},{"other_cache_delta",0},{"file_id",model_id},{"links",model_links},
        {"game_success_claimed",false},{"real_device_actions",0}}.dump(2);
    std::cout<<"PASS actual Application two-publication continuation, first external fixture Completed, second adopted UserStopped, distinct logical/budget growth\n";
    return 0;
}
int closure_application(const std::filesystem::path &pack, const std::filesystem::path &quests) {
    using A = wvd::app::ApplicationAssemblyTestAccess;
    const auto *configured = std::getenv("WVD_CLOSURE_ROOT");
    require_closure(configured && *configured, "WVD_CLOSURE_ROOT_REQUIRED");
    // 资源缓存本身包含完整哈希及 UUID；缩短隔离目录，避免 Windows MAX_PATH。
    const auto root = std::filesystem::path(configured) / ("app-" + wvd::platform::unique_id().substr(0, 8));
    std::filesystem::create_directories(root);
    const auto sentinel = root / "preexisting.log";
    { std::ofstream out(sentinel); out << "must-not-change"; }
    const auto sentinel_hash = wvd::platform::file_sha256(sentinel);
    auto backend = std::make_shared<OfflineConnection>(pack);
    wvd::app::Application app({root / "admission", pack, {}, quests}, backend);
    int prepared = 0;
    const auto blocked = [&](std::uintmax_t bytes, bool failure, const char *code) {
        A::space(app, bytes, failure);
        try { A::queue(app, "space-rejected", prepared); throw std::runtime_error("SPACE_ADMITTED"); }
        catch (const std::exception &error) { require_closure(std::string(error.what()) == code, error.what()); }
        require_closure(prepared == 0 && backend->connections == 0 && backend->captures == 0, "SPACE_SIDE_EFFECT");
    };
    blocked(1073741823, false, "RUN_STORAGE_SPACE_LOW");
    blocked(0, true, "RUN_STORAGE_SPACE_QUERY_FAILED");
    blocked(std::uintmax_t(-1), false, "RUN_STORAGE_SPACE_QUERY_FAILED");
    std::cout << "SPACE-01 PASS: low/unknown/query failure, no prepare/connect/capture\n";
    A::space(app, 2147483648ULL);
    const auto receipt = A::queue(app, "space-threshold", prepared);
    for (int i=0; i<200 && call(app, wvd::api::http::verb::get, "/api/v1/device").value("busy", false); ++i) std::this_thread::sleep_for(10ms);
    A::space(app, 0);
    const auto duplicate = A::queue(app, "space-threshold", prepared);
    require_closure(prepared == 1 && duplicate.at("request_id") == receipt.at("request_id"), "SPACE_IDEMPOTENCY_FAILED");
    std::cout << "SPACE-02 PASS: threshold admission and low-space existing receipt\n";
    for (const auto &fault : {std::string{"space"}, std::string{"timing"}, std::string{"terminal"}, std::string{"image"}, std::string{"unrecorded"}}) {
        auto isolated_backend = std::make_shared<OfflineConnection>(pack);
        wvd::app::Application isolated({root / fault, pack, {}, quests}, isolated_backend);
        auto &coordinator = A::coordinator(isolated);
        const auto inject_fault = [&] {
            if (fault == "space") return;
            if (fault == "image") { wvd::runtime::NativeCoordinatorTestAccess::image_failure(coordinator); return; }
            if (fault == "unrecorded") { wvd::runtime::NativeCoordinatorTestAccess::image_failure(coordinator, false); return; }
            const auto path = coordinator.run_directory() / (fault == "timing" ? "action-timing.jsonl" : "result.json");
            std::filesystem::create_directory(path);
        };
        coordinator.start(closure_definition(root / fault / "published" / fault, fault, inject_fault), isolated_backend);
        require_closure(coordinator.wait_for(15s), "SPACE_FIXTURE_NOT_QUIESCENT");
        const auto terminal = coordinator.snapshot();
        if (fault == "space") require_closure(terminal.completed_business_units == 3 && terminal.result_saved, "SPACE_COMPLETED_SOURCE_REQUIRED:" + terminal.reason);
        if (fault == "timing") require_closure(std::find(terminal.secondary_errors.begin(), terminal.secondary_errors.end(), "ACTION_TIMING_INCOMPLETE") != terminal.secondary_errors.end(), "TIMING_FAILURE_UNREPORTED");
        if (fault == "terminal") require_closure(!terminal.result_saved && !terminal.storage_error.empty(), "TERMINAL_FAILURE_UNREPORTED");
        if (fault == "image") require_closure(terminal.state == wvd::contracts::RunState::Completed &&
            terminal.secondary_errors == std::vector<std::string>{"DIAGNOSTIC_IMAGES_INCOMPLETE"}, "IMAGE_FAILURE_UNREPORTED");
        if (fault == "unrecorded") require_closure(terminal.secondary_errors == std::vector<std::string>{"DIAGNOSTIC_INCOMPLETE"},
            "UNRECORDED_FAILURE_DOWNGRADED");
        const auto connections = isolated_backend->connections.load();
        A::space(isolated, 0);
        A::watch(isolated, isolated_backend, fault);
        J status;
        const auto until = std::chrono::steady_clock::now() + 15s;
        do {
            status = call(isolated, wvd::api::http::verb::get, "/api/v1/runs/current");
            if (!status.at("repeat").value("active", false)) break;
            std::this_thread::sleep_for(20ms);
        } while (std::chrono::steady_clock::now() < until);
        require_closure(!status.at("repeat").value("active", true) && status.at("repeat").at("state") == "failed", "SPACE_REPEAT_NOT_STOPPED:" + status.dump());
        require_closure(isolated_backend->connections == connections && isolated_backend->inputs == 0, "SPACE_NEXT_RUN_SIDE_EFFECT");
        require_closure(status.at("repeat").at("completed_cycles") == (fault == "space" || fault == "image" ? 1 : 0), "SPACE_COMPLETED_COUNT_LOST");
        if (fault == "space" || fault == "image") require_closure(status.at("repeat").at("reason") == "RUN_STORAGE_SPACE_LOW", "SPACE_NEXT_REASON");
        std::ofstream(root / (fault + "-result.json")) << status.dump(2);
    }
    std::cout << "SPACE-03/04 PASS: image warning retains completed cycle; storage/timing/terminal faults block next run\n";
    require_closure(wvd::platform::file_sha256(sentinel) == sentinel_hash, "SPACE_EXISTING_FILE_CHANGED");
    std::cout << "SPACE-05 PASS: existing sentinel unchanged; fixture root=" << root.string() << '\n';
    return 0;
}
}

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--publication-maintenance-repeat-cli-fixture") {
            const auto fixture = publication_maintenance_contract::repeat_fixture(std::filesystem::absolute(wvd::platform::path_from_utf8(argv[2])));
            std::cout << fixture.dump() << '\n'; return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--publication-maintenance-cli-fixture")
            return publication_maintenance_contract::cli_fixture(std::filesystem::absolute(wvd::platform::path_from_utf8(argv[2])));
        if (argc == 3 && std::string(argv[1]) == "--publication-maintenance")
            return publication_maintenance_contract::run(std::filesystem::absolute(wvd::platform::path_from_utf8(argv[2])));
        if (argc == 6 && std::string(argv[1]) == "--publication-maintenance-child")
            return publication_maintenance_contract::child(std::filesystem::absolute(wvd::platform::path_from_utf8(argv[2])),argv[3],argv[4],argv[5]);
        if (argc == 5 && std::string(argv[1]) == "--resource-app-batch")
            return resource_application_batch(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]));
        if (argc == 3 && std::string(argv[1]) == "--resource-budget") {
            using A = wvd::app::ApplicationAssemblyTestAccess;
            const auto root = std::filesystem::absolute(argv[2]);
            require_closure(!std::filesystem::exists(root), "RESOURCE_BUDGET_FRESH_ROOT_REQUIRED");
            std::filesystem::create_directories(root / "asset-cache/retained");
            { std::ofstream output(root / "asset-cache/retained/payload"); output << "12345"; }
            const J hit{{"resource_new",false},{"resource_bytes",100},{"resource_manifest_bytes",20},
                {"metadata_bytes",7},{"overhead_bytes",11}};
            require_closure(A::budget(root, 13, hit) == 36, "BUDGET_REUSE_OR_PERSISTENT_CACHE_MISCOUNTED");
            auto first = hit; first["resource_new"] = true;
            require_closure(A::budget(root, 13, first) == 156, "BUDGET_NEW_RESOURCE_OMITTED");
            constexpr std::uint64_t GiB = 1024ULL*1024*1024;
            A::continue_batch(14*GiB);
            submission_contract::rejects([&] { A::continue_batch(14*GiB+1); }, "BATCH_STORAGE_RESERVE_LOW");
            submission_contract::rejects([&] { A::budget(root,16*GiB,hit); }, "BATCH_STORAGE_LIMIT");
            submission_contract::rejects([&] { A::budget(root,(std::numeric_limits<std::uint64_t>::max)(),hit); }, "BATCH_STORAGE_LIMIT");
            bool unknown = false;
            try { auto cost=hit;cost.erase("metadata_bytes");A::budget(root,0,cost); } catch(const std::exception &) { unknown=true; }
            require_closure(unknown,"BUDGET_UNKNOWN_TREATED_AS_ZERO");
            std::filesystem::create_directories(root / "published/.asset-sets" / std::string(64,'0'));
            { std::ofstream output(root / "published/.asset-sets" / std::string(64,'0') / "manifest.json"); output << "{}"; }
            bool unclosed=false;
            try { A::budget(root,0,hit); } catch(const std::exception &) { unclosed=true; }
            require_closure(unclosed,"BUDGET_UNCLOSED_MANIFEST_ACCEPTED");

            std::ofstream(root / "result.json") << J{{"passed",true},{"reuse_budget",36},{"first_budget",156},
                {"continue_threshold_bytes",14*GiB},{"hard_limit_bytes",16*GiB},{"unknown_rejected",true}}.dump(2);
            std::cout << "PASS resource budget persistent cache, unique hit/new cost, 14/16GiB, overflow and unknown\n";
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--preparation-ownership")
            return preparation_ownership_contract(std::filesystem::absolute(argv[2]),
                std::filesystem::absolute(argv[3]), std::filesystem::absolute(argv[4]));
        if (argc == 4 && std::string(argv[1]) == "--submission-crash")
            return submission_contract::interrupted(std::filesystem::absolute(argv[2]), std::string(argv[3]) == "replace");
        if (argc == 5 && std::string(argv[1]) == "--submission-readonly") {
            using A = wvd::app::ApplicationAssemblyTestAccess;
            const auto root = std::filesystem::absolute(argv[4]);
            require_closure(!std::filesystem::exists(root), "SUBMISSION_APP_ROOT_MUST_BE_FRESH");
            const auto pack = std::filesystem::absolute(argv[2]), quests = std::filesystem::absolute(argv[3]);
            const J request{{"request_id", "historical"}, {"task_id", "GiantBounty"}, {"resource_locale", "zh-Hant"}};
            const J intent{{"kind", "task"}, {"request", request}};
            {
                auto backend = std::make_shared<OfflineConnection>(pack);
                wvd::app::Application app({root / "data", pack, {}, quests}, backend);
                A::receipt(app, "historical", intent, 42);
            }
            const auto history = root / "data/requests";
            const auto old_path = submission_contract::record(history, "historical");
            const auto before = submission_contract::read_text(old_path);
            auto unknown_request = request; unknown_request["request_id"] = "unknown-history";
            {
                wvd::storage::SubmissionStore history_store(history);
                history_store.save("unknown-history", {{"kind", "task"}, {"request", unknown_request}},
                    {{"request_id", "unknown-history"}, {"state", "preparing"}}, false);
            }
            auto temporary = old_path; temporary += "." + wvd::platform::unique_id() + ".tmp";
            { std::ofstream out(temporary); out << "unfinished"; }
            auto backend = std::make_shared<OfflineConnection>(pack);
            wvd::app::Application app({root / "data", pack, {}, quests}, backend);
            const auto status = call(app, wvd::api::http::verb::get, "/api/v1/runs/current").at("request_history");
            require_closure(status.at("read_only") == true && status.at("pending_count") == 1 &&
                status.at("pending_intent_count") == 1,
                "READONLY_APP_NOT_DIAGNOSABLE");
            const auto replay = call(app, wvd::api::http::verb::post, "/api/v1/runs/start", request);
            require_closure(replay.at("run_id") == 42 && replay.at("replayed") == true, "READONLY_APP_OLD_ID_UNREADABLE");
            const auto unknown_replay = call(app, wvd::api::http::verb::post, "/api/v1/runs/start", unknown_request);
            require_closure(unknown_replay.at("state") == "preparing" && unknown_replay.at("replayed") == true,
                "READONLY_APP_UNKNOWN_RESULT_REEXECUTED");
            auto next = request; next["request_id"] = "new-intent";
            bool rejected{};
            try { call(app, wvd::api::http::verb::post, "/api/v1/runs/start", next); }
            catch (const std::exception &e) { rejected = std::string(e.what()).find("REQUEST_HISTORY_READ_ONLY") != std::string::npos; }
            require_closure(rejected && backend->connections == 0 && backend->captures == 0 && backend->inputs == 0,
                "READONLY_APP_STARTED_UNKNOWN_INTENT");
            require_closure(submission_contract::read_text(old_path) == before && std::filesystem::exists(temporary),
                "READONLY_APP_CHANGED_AUTHORITY_OR_TEMP");
            std::ofstream(root / "results.json") << J{{"old_run", 42}, {"new_intent_rejected", true},
                {"device_connections", backend->connections.load()}, {"captures", backend->captures.load()}, {"inputs", backend->inputs.load()},
                {"unknown_state_preserved", unknown_replay.at("state")},
                {"old_bytes_unchanged", true}, {"temp_preserved", true}}.dump(2);
            std::cout << "PASS actual Application: readonly history replay, new intent rejected before device/run/input\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--repository-read-oom") {
            const auto root = std::filesystem::absolute(argv[2]);
            require_closure(!std::filesystem::exists(root), "NEW_ISOLATED_ROOT_REQUIRED");
            std::ifstream source("resources/authoring/public-flows.json"); J documents; source >> documents;
            auto original = documents.at(0);
            original["flow"]["description"] = std::string(2000, 'x');
            wvd::storage::WorkflowRepository repository(root);
            const auto saved = repository.create(original);
            const auto id = saved.at("flow").at("id").get<std::string>();
            const auto path = root / (id + ".json");
            require_closure(repository.read(id) == saved, "REPOSITORY_WARM_READ_FAILED");
            const auto length = std::filesystem::file_size(path);
            const auto buffer_capacity = std::string(static_cast<std::size_t>(length), '\0').capacity() + 1;
            DWORD before{}, after{};
            require_closure(GetProcessHandleCount(GetCurrentProcess(), &before), "HANDLE_COUNT_FAILED");
            read_fault_file = path.c_str(); read_fault_after_open = false;
            read_allocation_fault = buffer_capacity;
            bool failed{};
            try { (void)repository.read(id); } catch (const std::bad_alloc &) { failed = true; }
            read_allocation_fault = 0; read_fault_file = nullptr;
            require_closure(failed && read_fault_after_open, "READ_BUFFER_FAULT_NOT_AFTER_HANDLE_ACQUISITION");
            require_closure(GetProcessHandleCount(GetCurrentProcess(), &after) && after == before,
                "READ_ALLOCATION_LEAKED_HANDLE");
            const auto writer = CreateFileW(path.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            require_closure(writer != INVALID_HANDLE_VALUE, "READ_OOM_LEFT_STALE_FILE_LOCK");
            CloseHandle(writer);
            require_closure(repository.read(id) == saved, "READ_OOM_RETRY_CHANGED_DOCUMENT");
            std::cout << "PASS repository read bad_alloc after acquisition: handles stable, lock released, original document retry intact\n";
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--control-contract") {
            using A = wvd::app::ApplicationAssemblyTestAccess;
            const auto root=std::filesystem::absolute(argv[4]);
            require_closure(!std::filesystem::exists(root),"NEW_ISOLATED_ROOT_REQUIRED");
            auto backend=std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            wvd::app::Application app({root/"data",std::filesystem::absolute(argv[2]),{},std::filesystem::absolute(argv[3])},backend);
            const auto catalog = call(app, wvd::api::http::verb::get, "/api/v1/catalog");
            require_closure(!catalog.at("semantic_resources").empty(), "FROZEN_SEMANTIC_LIBRARY_NOT_LOADED");
            const auto board = call(app, wvd::api::http::verb::get, "/api/v1/workflows/guild-open-bounty-page/builtin");
            require_closure(board.at("status") == "current", "FROZEN_PUBLIC_LIBRARY_NOT_LOADED");
            const auto giant = A::giant_graph(app);
            require_closure(giant.nodes.contains("Task_FirstDungeon_AfterTargetBattle0") &&
                backend->connections == 0 && backend->captures == 0 && backend->inputs == 0,
                "APPLICATION_GIANT_SNAPSHOT_MISSING_STEP_OR_TOUCHED_DEVICE");
            for (const auto &kind : {std::string("task"),std::string("workflow"),std::string("combat_debug")}) {
                J request{{"request_id","prior-"+kind},{"profile_revision","obsolete"},{"flow_revision","obsolete"},
                    {"resource_locale","zh-Hant"},{"task_id","removed-task"},{"flow_id","removed-flow"}};
                J intent{{"kind",kind},{"request",request}};
                if(kind=="workflow") intent["flow_id"]="removed-flow";
                A::receipt(app,"prior-"+kind,intent,42);
                const auto path=kind=="task" ? "/api/v1/runs/start" : kind=="workflow" ?
                    "/api/v1/workflows/removed-flow/run" : "/api/v1/combat/debug";
                const auto prior=call(app,wvd::api::http::verb::post,path,request);
                require_closure(prior.value("replayed",false) && prior.at("run_id")==42,"API_REPLAY_AFTER_MUTABLE_PREFLIGHT");
            }
            std::atomic<bool> entered{},release{}; int prepared{};
            A::space_reader(app,[&](const auto &)->std::filesystem::space_info {
                entered=true; while(!release) std::this_thread::sleep_for(5ms);
                return {4ULL<<30,4ULL<<30,4ULL<<30};
            });
            auto queued=std::async(std::launch::async,[&] {
                try { A::queue(app,"slow-space",prepared); return std::string("admitted"); }
                catch(const std::exception &e){return std::string(e.what());}
            });
            struct Release {std::atomic<bool> &flag;~Release(){flag=true;}} cleanup{release};
            const auto deadline=std::chrono::steady_clock::now()+3s;
            while(!entered && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(5ms);
            require_closure(entered,"SLOW_SPACE_NOT_ENTERED");
            const auto start=std::chrono::steady_clock::now();
            call(app,wvd::api::http::verb::post,"/api/v1/runs/current/stop");
            require_closure(std::chrono::steady_clock::now()-start<200ms,"STOP_BLOCKED_BY_PREFLIGHT_IO");
            release=true;
            require_closure(queued.get()=="PREPARATION_CANCELLED" && prepared==0,"STOP_LOST_DURING_PREFLIGHT");
            require_closure(backend->connections==0 && backend->captures==0 && backend->inputs==0,"CONTROL_FIXTURE_DEVICE_SIDE_EFFECT");
            std::cout<<"PASS actual Application API: old task/workflow/debug receipt before changed preflight; stop bypasses blocked disk admission and cancels preparation\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--submission-history") {
            const auto root = std::filesystem::absolute(argv[2]);
            require_closure(!std::filesystem::exists(root), "SUBMISSION_ROOT_MUST_BE_FRESH");
            {
                wvd::storage::SubmissionStore store(root / "durable");
                for (unsigned i = 0; i < 300; ++i) {
                    const auto id = "request-" + std::to_string(i);
                    const J intent{{"task_id", "GiantBounty"}, {"profile_revision", "original"}};
                    store.save(id, intent, {{"state", "preparing"}, {"request_id", id}}, false);
                    store.save(id, intent, {{"state", "submitted"}, {"run_id", i + 1}, {"request_id", id}}, true);
                }
            }
            wvd::storage::SubmissionStore restarted(root / "durable");
            const J original{{"task_id", "GiantBounty"}, {"profile_revision", "original"}};
            const auto replay = restarted.replay("request-0", original);
            require_closure(replay && replay->at("run_id") == 1 && replay->at("replayed") == true,
                "ORIGINAL_RECEIPT_LOST_AFTER_256_OR_RESTART");
            bool conflict{};
            try { restarted.replay("request-0", {{"task_id", "Scorpionesses"}}); }
            catch (const std::exception &e) { conflict = std::string(e.what()) == "IDEMPOTENCY_CONFLICT"; }
            require_closure(conflict, "DIFFERENT_INTENT_REUSED_ID");
            require_closure(!restarted.replay("new-request", original), "NEW_REQUEST_REFUSED_BY_HISTORY");
            std::cout << "PASS production durable receipts: 300 completed intents, process restart, original run, identity conflict\n";
            submission_contract::run(root / "boundaries");
            return 0;
        }
        if(argc==2 && std::string(argv[1])=="--measurement-contract") {
            wvd::runtime::MeasurementBarrier gate;
            const auto memory=wvd::platform::sample_memory();
            const J endpoint{{"phase","worker_joined"},{"release_scope","worker"},
                {"input_clean",true},{"cleanup_complete",true},{"heap_maintenance_complete",true},
                {"process_id",memory.process_id},{"process_created_100ns",memory.process_created_100ns}};
            require_closure(!gate.armed(),"MEASUREMENT_NOT_DEFAULT_OFF");
            gate.arm("owned",{{"capture","isolated"}},2,250ms,2s);
            for(unsigned i=1;i<=2;++i) {
                auto held=std::async(std::launch::async,[&]{gate.joined_boundary(endpoint,[]{return false;});});
                const auto deadline=std::chrono::steady_clock::now()+1s;
                while(!gate.status().at("held").get<bool>() && std::chrono::steady_clock::now()<deadline)
                    std::this_thread::sleep_for(2ms);
                require_closure(gate.status().at("held")==true,"MEASUREMENT_NOT_HELD");
                bool rejected{};
                try {gate.release("foreign",i);} catch(const std::exception &){rejected=true;}
                require_closure(rejected,"MEASUREMENT_FOREIGN_RELEASE");
                gate.release("owned",i);held.get();gate.release("owned",i);
            }
            require_closure(!gate.armed() && gate.status().at("state")=="completed","MEASUREMENT_PAIR_NOT_RELEASED");
            gate.arm("lost-collector",J::object(),2,30ms,100ms);
            gate.joined_boundary(endpoint,[]{return false;});
            require_closure(!gate.armed() && gate.status().at("state")=="timed_out","MEASUREMENT_COLLECTOR_LOSS_HANGS");
            gate.arm("cancelled",J::object(),2,50ms,100ms);
            gate.joined_boundary(endpoint,[]{return true;});
            require_closure(!gate.armed() && gate.status().at("state")=="cancelled","MEASUREMENT_CANCEL_HANGS");
            std::cout<<"PASS production measurement barrier: default off, two acknowledged joins, foreign identity rejected, expiry and cancel bounded\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--builtin-transaction") {
            const auto root=std::filesystem::absolute(argv[2]);
            require_closure(!std::filesystem::exists(root),"TRANSACTION_ROOT_MUST_BE_FRESH");
            std::filesystem::create_directories(root);
            std::ifstream source("resources/authoring/public-flows.json"); J documents; source>>documents;
            auto original=documents.at(0);
            const auto id=original.at("flow").at("id").get<std::string>();
            for(unsigned stage=0;stage<4;++stage) {
                const auto directory=root/std::to_string(stage);
                wvd::storage::WorkflowRepository repository(directory);
                const auto before=repository.create(original);
                repository.register_builtin(before,before.at("revision"));
                auto proposed=original; proposed["flow"]["description"]="isolated audited update";
                const auto after=repository.inspect_builtin(proposed).at("builtin_revision").get<std::string>();
                const auto updated=repository.sync_builtin(proposed,before.at("revision"),after);
                const auto transactions=directory/".builtin-transactions";
                std::filesystem::path completed;
                for(const auto &entry:std::filesystem::directory_iterator(transactions))
                    if(entry.path().filename().string().ends_with(".completed.json")) completed=entry.path();
                require_closure(!completed.empty(),"TRANSACTION_HISTORY_MISSING");
                std::ifstream record(completed);J intent;record>>intent;record.close();
                std::filesystem::rename(completed,completed.string()+".fixture-history");
                const auto body=stage==0?before:updated;
                const auto metadata=stage<2?intent.at("before_metadata"):intent.at("after_metadata");
                wvd::platform::atomic_write(directory/(id+".json"),body.dump(2),true);
                wvd::platform::atomic_write(directory/".builtin"/(id+".json"),metadata.dump(2),true);
                if(stage==3) intent["after_metadata"]["local_revision"]=before.at("revision");
                wvd::platform::atomic_write(transactions/(id+".pending.json"),intent.dump(2),false);
                if(stage==3) {
                    bool rejected{};
                    try {wvd::storage::WorkflowRepository recovered(directory);}
                    catch(const std::exception &e) {rejected=std::string(e.what()).starts_with("BUILTIN_TRANSACTION_METADATA_INVALID");}
                    require_closure(rejected,"TAMPERED_TRANSACTION_COMMITTED");
                    continue;
                }
                wvd::storage::WorkflowRepository recovered(directory);
                require_closure(recovered.read(id)==updated,"TRANSACTION_RECOVERY_BODY_MISMATCH");
                require_closure(recovered.sync_builtin(proposed,before.at("revision"),after)==updated,
                    "TRANSACTION_RETRY_NOT_IDEMPOTENT");
                const auto backup=directory/".builtin-backups"/id/before.at("revision").get<std::string>()/"document.json";
                std::ifstream saved(backup);J old;saved>>old;
                require_closure(old==before,"TRANSACTION_OLD_BODY_LOST");
                auto local=updated;local["flow"]["description"]="local user edit";
                recovered.compare_exchange(id,after,local);
                bool conflict{};
                try {(void)recovered.sync_builtin(proposed,before.at("revision"),after);}
                catch(const std::exception &e) {conflict=std::string(e.what()).starts_with("BUILTIN_LOCAL_CONFLICT");}
                require_closure(conflict,"TRANSACTION_OVERWROTE_LOCAL_EDIT");
            }
            std::cout<<"PASS real repository transaction: pre-body/body-only/committed restart, exact old backup, retry, metadata tamper and CAS conflict\n";
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--stop-release") {
            using A = wvd::app::ApplicationAssemblyTestAccess;
            const auto root = std::filesystem::absolute(argv[4]);
            require_closure(!std::filesystem::exists(root), "STOP_RELEASE_ROOT_MUST_BE_FRESH");
            std::filesystem::create_directories(root);
            auto backend = std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            wvd::app::Application app({root / "data", std::filesystem::absolute(argv[2]), {}, std::filesystem::absolute(argv[3])}, backend);
            auto &coordinator = A::coordinator(app);
            std::atomic<bool> entered{false}, release{false};
            auto definition = closure_definition(root / "data/published/stop-release", "stop-release", [&] {
                entered = true;
                while (!release) std::this_thread::sleep_for(5ms);
            });
            definition.logging.memory = true;
            definition.logging.level = wvd::storage::LogLevel::Info;
            coordinator.start(std::move(definition), backend);
            // Always release the isolated worker if an assertion throws.
            struct Release { std::atomic<bool> &flag; ~Release() { flag = true; } } cleanup{release};
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
            require_closure(entered, "STOP_WORKER_NOT_ENTERED");
            A::watch(app, backend, "stop-release");
            call(app, wvd::api::http::verb::post,
                "/api/v1/runs/" + std::to_string(coordinator.snapshot().run_id) + "/stop");
            std::this_thread::sleep_for(350ms);
            const auto pending = call(app, wvd::api::http::verb::get, "/api/v1/runs/current");
            require_closure(pending.at("repeat").at("active") == true && pending.at("busy") == true,
                            "STOP_RELEASE_WATCHER_EXITED_BEFORE_WORKER");
            release = true;
            J ended;
            do {
                std::this_thread::sleep_for(20ms);
                ended = call(app, wvd::api::http::verb::get, "/api/v1/runs/current");
            } while (ended.at("busy") == true && std::chrono::steady_clock::now() < deadline);
            require_closure(ended.at("busy") == false && ended.at("state") == "UserStopped" &&
                ended.at("repeat").at("completed_cycles") == 0 && backend->inputs == 0, "STOP_RELEASE_TERMINAL_INVALID");
            std::ifstream memory(coordinator.run_directory() / "memory-lifecycle.json");
            const auto boundaries = J::parse(memory);
            for (const auto *phase : {"worker_definition_released", "worker_joined", "heap_resources_optimized", "batch_payloads_released"})
                require_closure(boundaries.at("samples").contains(phase), "STOP_RELEASE_BOUNDARY_MISSING");
            std::ofstream(root / "result.json") << J{{"passed", true}, {"pending", pending},
                {"ended", ended}, {"memory", boundaries}, {"game_inputs", 0}}.dump(2);
            app.stop();
            std::cout << "Stop waits for actual worker release without blocking the control API; all four boundaries recorded\n";
            return 0;
        }
        if (argc == 6 && std::string(argv[1]) == "--prepare-device") {
            const auto root = std::filesystem::absolute(argv[5]);
            if (std::filesystem::exists(root)) throw std::runtime_error("DEVICE_PREPARATION_ROOT_MUST_BE_FRESH");
            std::filesystem::create_directories(root);
            std::filesystem::copy_file(argv[4], root / "profile.json");
            std::ifstream input(argv[4]); const auto stored = J::parse(input);
            auto initial = std::make_shared<BindingConnection>();
            wvd::app::Application app({root, std::filesystem::absolute(argv[2]), {}, std::filesystem::absolute(argv[3])}, initial);
            J cases = J::array();
            for (const auto &name : {"alive", "exited", "offline", "observation_failed", "cleanup_failed", "binding_changed", "stale", "identity_changed", "cancelled_after_observation", "cancelled_before_observation"}) {
                auto old = std::make_shared<BindingConnection>();
                std::shared_ptr<BindingConnection> next;
                int connections = 0;
                const std::string mode(name);
                if (mode == "exited" || mode == "cleanup_failed") { old->exited = true; old->running = old->connected = false; }
                if (mode == "offline") old->connected = false;
                old->cleanup_fail = mode == "cleanup_failed";
                old->probe_fail = mode == "observation_failed";
                old->binding_matches = mode != "binding_changed";
                old->stale = mode == "stale"; old->mismatch = mode == "identity_changed";
                if (mode == "cancelled_after_observation") old->after_observation = [&] { wvd::app::ApplicationAssemblyTestAccess::cancel(app, true); };
                wvd::app::ApplicationAssemblyTestAccess::connection(app, old, [&](const J &binding) {
                    require_closure(old->disconnected == 1 && !wvd::app::ApplicationAssemblyTestAccess::backend(app), "RECONNECT_BEFORE_DISPOSAL");
                    require_closure(binding.at("emulator_path") == stored.at("values").at("EMU_PATH") &&
                        binding.at("emulator_index") == stored.at("values").at("EMU_INDEX") &&
                        binding.at("adb_address") == stored.at("values").at("ADB_ADRESS"), "RECONNECTED_WRONG_BINDING");
                    ++connections; next = std::make_shared<BindingConnection>();
                    wvd::app::ApplicationAssemblyTestAccess::set_backend(app, next);
                });
                if (mode == "cancelled_before_observation") wvd::app::ApplicationAssemblyTestAccess::cancel(app, true);
                bool failed = false;
                try {
                    const auto result = wvd::app::ApplicationAssemblyTestAccess::ensure(app, stored);
                    require_closure(mode == "alive" || mode == "exited", "UNCONFIRMED_PREPARATION_SUCCEEDED");
                    require_closure(result == (mode == "alive" ? old : next), "WRONG_BACKEND_RETURNED");
                    if (mode == "exited") {
                        const wvd::devices::LifecyclePlan plan{next->target,
                            {wvd::devices::LifecycleOperation::EnsureVpn, wvd::devices::LifecycleOperation::StartApplication}, 1};
                        require_closure(wvd::devices::initial_lifecycle_plan(plan) &&
                            wvd::devices::execute_lifecycle_plan(plan, *next, [] { return false; }, [](const auto &, const auto &) {}) ==
                                wvd::devices::LifecycleEnd::ReadyForBoot && next->actions == 2, "EXISTING_STARTUP_CHAIN_NOT_PRESERVED");
                    }
                } catch (const std::exception &error) {
                    if (mode == "alive" || mode == "exited") throw;
                    const std::map<std::string, std::string> expected{{"offline", "DEVICE_START_OBSERVATION_UNCONFIRMED"},
                        {"observation_failed", "FIXTURE_OBSERVATION_FAILED"}, {"cleanup_failed", "DEVICE_CLEANUP_PENDING"},
                        {"binding_changed", "DEVICE_BINDING_CHANGED_RECONNECT_REQUIRED"}, {"stale", "DEVICE_START_OBSERVATION_INVALID"},
                        {"identity_changed", "DEVICE_START_OBSERVATION_INVALID"}, {"cancelled_after_observation", "PREPARATION_CANCELLED"},
                        {"cancelled_before_observation", "PREPARATION_CANCELLED"}};
                    require_closure(error.what() == expected.at(mode), "WRONG_PREPARATION_FAILURE:" + std::string(error.what()));
                    failed = true;
                }
                wvd::app::ApplicationAssemblyTestAccess::cancel(app, false);
                require_closure(connections == (mode == "exited" ? 1 : 0), "UNAUTHORIZED_RECONNECT");
                if (mode != "exited") require_closure(wvd::app::ApplicationAssemblyTestAccess::backend(app) == old,
                    "OLD_OWNER_DROPPED_BEFORE_CONFIRMED_CLEANUP");
                require_closure((mode == "alive" || mode == "exited") != failed, "PREPARATION_OUTCOME_WRONG");
                require_closure(old->read_deadline == std::chrono::steady_clock::time_point{} &&
                    old->bounded_reads == old->cleared_reads && old->bounded_reads == old->observed,
                    "PREPARATION_READ_WINDOW_ESCAPED");
                cases.push_back({{"case", mode}, {"passed", true}, {"connections", connections}, {"disconnects", old->disconnected}});
            }
            auto offline = std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            auto &coordinator = wvd::app::ApplicationAssemblyTestAccess::coordinator(app);
            std::promise<void> release; auto allowed = release.get_future().share();
            auto definition = closure_definition(root / "busy-fixture", "p02-previous-active", [allowed] { allowed.wait(); });
            coordinator.start(std::move(definition), offline);
            auto old = std::make_shared<BindingConnection>();
            wvd::app::ApplicationAssemblyTestAccess::connection(app, old, [](const J &) { throw std::runtime_error("ACTIVE_RECONNECT_FORBIDDEN"); });
            bool blocked = false;
            try { (void)wvd::app::ApplicationAssemblyTestAccess::ensure(app, stored); }
            catch (const std::exception &error) { blocked = std::string(error.what()) == "DEVICE_PREPARATION_NOT_QUIESCENT"; }
            release.set_value(); coordinator.request_stop();
            require_closure(coordinator.wait_for_worker(5s), "PREVIOUS_WORKER_NOT_CLEANED");
            require_closure(blocked && old->disconnected == 0 && old->observed == 0, "ACTIVE_OWNER_TOUCHED");
            cases.push_back({{"case", "previous_run_active"}, {"passed", true}});
            for (const bool performance : {true, false}) {
                const auto id = performance ? "p01-start-log-on" : "p01-start-log-off";
                auto ready = closure_definition(root / "published" / id, id);
                auto &bundle = ready.units.front().bundle;
                bundle.lease = wvd::platform::BundleLease::open_publication(bundle.root);
                ready.logging.performance = performance;
                ready.preparation = {{"fixture", "logging-switch-only"}};
                (void)wvd::app::ApplicationAssemblyTestAccess::commit(app, std::move(ready), offline);
                require_closure(coordinator.wait_for_worker(5s), "START_LOG_WORKER_NOT_JOINED");
                const auto log = coordinator.run_directory() / "diagnostics.jsonl";
                int rows = 0;
                if (std::filesystem::exists(log)) {
                    std::ifstream stream(log); std::string line;
                    while (std::getline(stream, line)) {
                        const auto value = J::parse(line);
                        if (value.value("type", "") == "preparation.completed") {
                            ++rows;
                            require_closure(value.at("payload").at("coordinator_start").at("wall_ms").get<double>() >= 0,
                                            "START_DURATION_NOT_RECORDED");
                        }
                    }
                }
                require_closure(rows == (performance ? 1 : 0), "PREPARATION_LOGGING_SWITCH_IGNORED");
                const auto live = wvd::app::ApplicationAssemblyTestAccess::preparation_status(app, stored, performance);
                require_closure(live.contains("preparation") == performance, "LIVE_PREPARATION_SWITCH_IGNORED");
                if (performance) {
                    const auto &phase = live.at("preparation").at("compile_task_graph");
                    require_closure(phase.at("state") == "completed" && phase.at("wall_ms").get<double>() >= 0 &&
                        live.at("preparation").at("current_phase") == "compile_task_graph",
                        "LIVE_PREPARATION_PHASE_NOT_RECORDED");
                }
                cases.push_back({{"case", id}, {"passed", true}, {"preparation_log_rows", rows}});
            }
            require_closure(!wvd::app::ApplicationAssemblyTestAccess::preparation_status(
                app, stored, true, wvd::storage::LogLevel::Warn).contains("preparation"),
                "LIVE_PREPARATION_LOG_LEVEL_IGNORED");
            std::ofstream report(root / "result.json"); report << J{{"passed", true}, {"cases", cases},
                {"external_dependency", "MuMu connection/observation adapter isolated; no real device operations"}}.dump(2);
            report.close(); require_closure(bool(report), "DEVICE_RESULT_WRITE_FAILED"); app.stop();
            std::cout << "Application cold preparation: live reuse, explicit exit cleanup/rebinding, unknown/cancel/active ownership guards passed\n";
            return 0;
        }
        if ((argc == 8 || argc == 9) && std::string(argv[1]) == "--publication-prepare") {
            const bool current_memory = argc == 9 && std::string(argv[8]) == "--current-memory";
            const bool memory_cycles = current_memory || (argc == 9 && std::string(argv[8]) == "--memory-cycles");
            require_closure(argc == 8 || memory_cycles, "PREPARATION_OPTION_UNKNOWN");
            const auto root = std::filesystem::absolute(argv[7]);
            if (std::filesystem::exists(root)) throw std::runtime_error("PREPARATION_TEST_ROOT_MUST_BE_FRESH");
            std::filesystem::create_directories(root);
            std::filesystem::copy_file(argv[4], root / "profile.json");
            const auto workflows = std::filesystem::path(argv[4]).parent_path() / "workflows";
            if (std::filesystem::exists(workflows)) std::filesystem::copy(workflows, root / "workflows",
                std::filesystem::copy_options::recursive);
            std::ifstream profile(argv[4]); const auto stored = J::parse(profile);
            auto backend = std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            wvd::app::Application app({root, std::filesystem::absolute(argv[2]), {}, std::filesystem::absolute(argv[3])}, backend);
            if (std::getenv("WVD_PREPARATION_GATE")) {
                { std::ofstream ready(root / "capture-ready"); ready << GetCurrentProcessId(); }
                const auto deadline = std::chrono::steady_clock::now() + 30s;
                while (!std::filesystem::exists(root / "capture-start")) {
                    if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("PREPARATION_CAPTURE_GATE_TIMEOUT");
                    std::this_thread::sleep_for(20ms);
                }
            }
            std::ofstream phases(root / "phase-events.jsonl");
            const auto observer = [&](const char *phase, const char *state, const J &metrics) {
                phases << J{{"phase", phase}, {"state", state}, {"metrics", metrics},
                    {"pid", GetCurrentProcessId()}, {"steady_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count()}}.dump() << '\n';
                phases.flush();
                require_closure(bool(phases), "PREPARATION_PHASE_WRITE_FAILED");
            };
            J cycle_reports = J::array();
            J first_identity;
            std::map<std::string, std::string> first_files;
            for (int cycle = 0; cycle < (memory_cycles ? 4 : 1); ++cycle) {
            wvd::app::ApplicationAssemblyTestAccess::cancel(app, false);
            const auto before = wvd::platform::sample_memory();
            auto definition = wvd::app::ApplicationAssemblyTestAccess::prepare_giant(app, stored, observer);
            const auto &bundle = definition.units.front().bundle;
            { std::ofstream metrics(root / "preparation-metrics.json"); metrics << definition.preparation.dump(2); }
            const auto baseline = std::filesystem::absolute(argv[5]);
            if (current_memory) {
                std::ifstream identity(bundle.root / "program/identity.json");
                const auto value = J::parse(identity);
                std::map<std::string, std::string> files;
                for (const auto &file : bundle.files) {
                    require_closure(wvd::platform::file_sha256(bundle.lease->physical_member(file.relative_path)) == file.sha256,
                                    "CURRENT_PUBLISHED_HASH_INVALID");
                    files.emplace(file.relative_path, file.sha256);
                }
                if (!cycle) { first_identity = value; first_files = files; }
                require_closure(value == first_identity && files == first_files, "REPEATED_PREPARATION_CHANGED");
            } else {
                std::ifstream old_identity(baseline / "program/identity.json"), new_identity(bundle.root / "program/identity.json");
                require_closure(J::parse(old_identity) == J::parse(new_identity), "PREPARATION_BASELINE_IDENTITY_CHANGED");
            }
            if (!current_memory) for (const auto &file : bundle.files)
                require_closure(wvd::platform::file_sha256(baseline / wvd::platform::BundleLease::checked_relative(file.relative_path)) == file.sha256,
                                "PREPARATION_BASELINE_FILE_CHANGED:" + file.relative_path);
            require_closure(definition.preparation.at("source_model_bytes_after_copy") == 0,
                            "PREPARATION_MODEL_BUFFERS_RETAINED");
            require_closure(backend->inputs == 0, "PREPARATION_DEVICE_INPUT_OCCURRED");
            J report_data{{"metrics", definition.preparation},
                {"program_revision", bundle.revision}, {"game_inputs", backend->inputs.load()},
                {"tracking", std::getenv("WVD_PREPARATION_GATE") ? "PID HeapSnapshots" : "disabled"},
                {"comparison", current_memory ? "same current workflow identity and all hashes across four preparations" : "candidate121 run18 identity and all published hashes"}};
            const auto path = bundle.root;
            wvd::app::ApplicationAssemblyTestAccess::cancel(app, true);
            bool cancelled = false;
            std::string commit_error;
            try { (void)wvd::app::ApplicationAssemblyTestAccess::commit(app, std::move(definition), backend); }
            catch (const std::exception &error) { commit_error = error.what(); cancelled = commit_error == "PREPARATION_CANCELLED"; }
            { std::ofstream evidence(root / "commit-observed.json"); evidence << J{{"error", commit_error},
                {"cancelled", cancelled}, {"publication_exists", std::filesystem::exists(path)},
                {"run_id", wvd::app::ApplicationAssemblyTestAccess::coordinator(app).snapshot().run_id},
                {"inputs", backend->inputs.load()}}.dump(2); }
            require_closure(cancelled && !std::filesystem::exists(path) &&
                wvd::app::ApplicationAssemblyTestAccess::coordinator(app).snapshot().run_id == 0 && backend->inputs == 0,
                "FINAL_COMMIT_CANCEL_SUBMITTED_OR_LEFT_PUBLICATION");
            { std::ofstream gate(root / "commit-cancellation.json"); gate << J{{"passed", true},
                {"run_id", 0}, {"game_inputs", 0}, {"unstarted_publication_removed", true}}.dump(2); }
            report_data["passed"] = true;
            report_data["final_commit_cancelled_without_run"] = true;
            if (memory_cycles) {
                const auto heap = wvd::platform::optimize_idle_heap();
                require_closure(before.process_ok && heap.before.process_ok && heap.after.process_ok && heap.succeeded,
                                "PREPARATION_MEMORY_MEASUREMENT_FAILED");
                report_data["memory"] = {{"cycle", cycle + 1}, {"before", before.private_bytes},
                    {"released", heap.before.private_bytes}, {"optimized", heap.after.private_bytes},
                    {"heap_available", heap.heap_after.available}, {"heap_complete", heap.heap_after.complete},
                    {"heap_allocated", heap.heap_after.allocated}, {"heap_committed", heap.heap_after.committed},
                    {"optimize_us", heap.elapsed_us}, {"heap_summary_us", heap.heap_after.elapsed_us}};
            }
            cycle_reports.push_back(std::move(report_data));
            }
            std::ofstream output(argv[6]); output << (memory_cycles ? cycle_reports : cycle_reports.front()).dump(2);
            output.close(); require_closure(bool(output), "PREPARATION_RESULT_WRITE_FAILED");
            app.stop();
            std::cout << (current_memory ? "Current Giant preparation repeated with identical identity/hashes" : "Frozen Giant preparation identity and all hashes match candidate121")
                      << "; model cache=0; game inputs=0\n";
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--combat-editor") {
            const auto isolated = std::getenv("WVD_COMBAT_TEST_ROOT");
            if (!isolated) throw std::runtime_error("WVD_COMBAT_TEST_ROOT_REQUIRED");
            const auto root = std::filesystem::absolute(isolated);
            if (std::filesystem::exists(root)) throw std::runtime_error("COMBAT_TEST_ROOT_MUST_BE_FRESH");
            std::filesystem::create_directories(root);
            std::filesystem::copy_file(argv[4], root / "profile.json");
            auto backend = std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            wvd::app::Application app({root, std::filesystem::absolute(argv[2]), {}, std::filesystem::absolute(argv[3])}, backend);
            const auto before = wvd::platform::file_sha256(root / "profile.json");
            const auto profile = call(app, wvd::api::http::verb::get, "/api/v1/profile");
            std::ifstream input(root / "profile.json"); const auto stored = J::parse(input);
            auto definition = wvd::app::ApplicationAssemblyTestAccess::prepare_debug(app, stored);
            require_closure(definition.units.size() == 1 && !definition.startup && !definition.recovery, "DEBUG_SCOPE_INVALID");
            for (const auto &[id, flow] : definition.units[0].program->definitions)
                for (const auto &[key, step] : flow.steps)
                    require_closure(key.find("Boot_") == std::string::npos, "DEBUG_BOOT_PRESENT");
            const auto &values = stored.at("values");
            wvd::app::ApplicationAssemblyTestAccess::check_portraits(app, values);
            auto enemies = wvd::app::ApplicationAssemblyTestAccess::prepare_debug(app, stored, false);
            bool published_custom = false;
            for (const auto &file : enemies.units.at(0).bundle.files)
                published_custom |= file.relative_path.starts_with("image/custom/monster_");
            require_closure(published_custom, "CUSTOM_PORTRAIT_NOT_PUBLISHED");
            const auto &rules = values.at("TASK_POINT_STRATEGY").at("special_combat").at("rules");
            wvd::games::CombatStrategy strategy(values);
            strategy.reload(0);
            for (const auto &rule : rules) {
                strategy.begin_encounter(true, rule.at("id"));
                require_closure(strategy.summary().at("current").at("group_name") == rule.at("strategy"), "ENEMY_SCHEME_NOT_SELECTED");
            }
            auto city_request = J{{"strategy_name", values.at("STRATEGY")[0].at("group_name")},
                {"profile_revision", profile.at("revision")}, {"request_id", "combat-editor-city-rejection"}, {"resource_locale", "zh-Hant"}};
            (void)call(app, wvd::api::http::verb::post, "/api/v1/combat/debug", city_request);
            J status;
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            do { std::this_thread::sleep_for(50ms); status = call(app, wvd::api::http::verb::get, "/api/v1/device"); }
            while (status.at("operation").value("state", "") == "running" && std::chrono::steady_clock::now() < deadline);
            require_closure(status.at("operation").value("error", "").find("COMBAT_DEBUG_NOT_IN_BATTLE") != std::string::npos,
                "DEBUG_CITY_NOT_REJECTED:" + status.dump());
            require_closure(backend->inputs == 0 && before == wvd::platform::file_sha256(root / "profile.json"), "DEBUG_INPUT_OR_PROFILE_SIDE_EFFECT");
            app.stop();
            std::cout << "Combat debug native compilation/publication, monster selection and frozen portraits passed; city rejected, inputs=0, profile unchanged\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--logging-profile") {
            const auto root = std::filesystem::temp_directory_path() /
                ("wvd-logging-profile-" + wvd::platform::unique_id());
            auto backend = std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            wvd::app::Application application({root, std::filesystem::absolute(argv[2]), {},
                std::filesystem::absolute(argv[3])}, backend);
            const auto original = call(application, wvd::api::http::verb::get, "/api/v1/profile");
            auto logging = original.at("logging");
            if (logging.at("level") != "info" || logging.at("memory_interval_ms") != 1000)
                throw std::runtime_error("LOGGING_PROFILE_DEFAULT");
            logging["level"] = "debug";
            logging["memory_interval_ms"] = 5000;
            logging["recognition"] = true;
            const auto saved = call(application, wvd::api::http::verb::put, "/api/v1/profile",
                {{"revision", original.at("revision")}, {"profile", original.at("profile")},
                 {"logging", logging}});
            const auto loaded = call(application, wvd::api::http::verb::get, "/api/v1/profile");
            if (saved.at("revision") == original.at("revision") ||
                loaded.at("revision") != saved.at("revision") ||
                loaded.at("logging") != logging || loaded.at("profile") != original.at("profile"))
                throw std::runtime_error("LOGGING_PROFILE_NOT_PERSISTED");
            logging["memory_interval_ms"] = 5;
            bool rejected = false;
            try {
                (void)call(application, wvd::api::http::verb::put, "/api/v1/profile",
                    {{"revision", saved.at("revision")}, {"profile", saved.at("profile")},
                     {"logging", logging}});
            } catch (const std::runtime_error &) { rejected = true; }
            if (!rejected || call(application, wvd::api::http::verb::get,
                "/api/v1/profile").at("revision") != saved.at("revision"))
                throw std::runtime_error("LOGGING_PROFILE_INVALID_CHANGED_STORE");
            application.stop();
            std::cout << "Logging profile API, CAS and validation passed\n";
            return 0;
        }
        if (argc == 6 && std::string(argv[1]) == "--critical-author-entry") {
            // 输入为原保存文档/配置的只读副本；仅运行真实装配/发布准备，不 start Session。
            const auto root = std::filesystem::temp_directory_path() / ("wvd-author-entry-" + wvd::platform::unique_id());
            std::filesystem::create_directories(root / "workflows");
            std::ifstream original(argv[4]), profile(argv[5]);
            const auto document = J::parse(original), stored = J::parse(profile);
            const auto id = document.at("flow").at("id").get<std::string>();
            std::filesystem::copy_file(argv[4], root / "workflows" / (id + ".json"));
            auto backend = std::make_shared<OfflineConnection>(std::filesystem::absolute(argv[2]));
            wvd::app::Application application({root, std::filesystem::absolute(argv[2]), {}, std::filesystem::absolute(argv[3])}, backend);
            // 第一机会 C++ 异常的栈在展开前采集，调试限定在这个隔离入口。
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);
            const auto handler = AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS *info) -> LONG {
                if (info->ExceptionRecord->ExceptionCode != 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
                void *frames[32]; const auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
                std::cerr << "FIRST_CHANCE_CPP_EXCEPTION\n";
                for (USHORT i = 0; i < count; ++i) {
                    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
                    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
                    DWORD64 displacement{};
                    if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol)) {
                        std::cerr << symbol->Name << "+" << displacement;
                        IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line); DWORD offset{};
                        if (SymGetLineFromAddr64(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]), &offset, &line))
                            std::cerr << " " << line.FileName << ":" << line.LineNumber;
                        std::cerr << '\n';
                    }
                }
                return EXCEPTION_CONTINUE_SEARCH;
            });
            try {
                const auto definition = wvd::app::ApplicationAssemblyTestAccess::prepare_author(application, document, stored);
                if (definition.units.empty()) throw std::runtime_error("AUTHOR_PREPARE_EMPTY");
                const auto &program = *definition.units.front().program;
                const auto &root_definition = program.definitions.at(program.root_definition);
                if (root_definition.handoffs != std::set<std::string>{"blocked", "chest", "revive"})
                    throw std::runtime_error("AUTHOR_BUSINESS_HANDOFF_NOT_PRESERVED");
                std::cout << "PREPARED revision=" << document.at("revision") << " units=" << definition.units.size()
                          << " source=" << root.string() << '\n';
                std::cout << "root handoffs=blocked,chest,revive; unhandled exits remain ExternalBlocked, not success\n";
                RemoveVectoredExceptionHandler(handler); SymCleanup(GetCurrentProcess());
            } catch (...) { RemoveVectoredExceptionHandler(handler); SymCleanup(GetCurrentProcess()); throw; }
            auto invalid = document;
            invalid.erase("revision");
            invalid["nodes"][0]["parameters"]["binding"] = 123;
            wvd::api::Request request{wvd::api::http::verb::post, "/api/v1/workflows", 11};
            request.body() = invalid.dump(); request.prepare_payload();
            const auto rejected = application.handle(request);
            if (static_cast<unsigned>(rejected.status) < 400 ||
                rejected.body.find("AUTHOR_BUSINESS_PARAMETERS_INVALID") == std::string::npos ||
                rejected.body.find("battle") == std::string::npos)
                throw std::runtime_error("AUTHOR_INVALID_FIELD_NOT_REJECTED");
            std::cout << "INVALID /nodes/0/parameters/binding number rejected: " << rejected.body << '\n';
            if (backend->connections || backend->captures || backend->inputs)
                throw std::runtime_error("AUTHOR_PREPARE_DEVICE_SIDE_EFFECT");
            std::cout << "device connections=0 captures=0 inputs=0\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--closure-application")
            return closure_application(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        if (argc != 3) throw std::runtime_error("PACK_AND_QUEST_PATH_REQUIRED");
        const auto pack = std::filesystem::absolute(argv[1]);
        const auto quests = std::filesystem::absolute(argv[2]);
        const auto data = std::filesystem::temp_directory_path() /
            ("wvd-native-product-" + wvd::platform::unique_id());
        auto backend = std::make_shared<OfflineConnection>(pack);
        const J document{{"schema", 1},
            {"flow", {{"id", "offline-product-check"}, {"name", "Offline product check"},
                      {"description", "Bounded native application integration"}}},
            {"entry", "Wait"},
            {"nodes", J::array({
                J{{"id", "Wait"}, {"type", "wait"}, {"name", "Wait"},
                  {"parameters", {{"duration_ms", 10}}}},
                J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
                  {"parameters", {{"outcome", "success"}}}}})},
            {"edges", J::array({J{{"id", "wait_done"}, {"from", "Wait"},
                                      {"to", "Done"}, {"outcome", "success"}, {"order", 0}}})},
            {"layout", {{"nodes", J::array({J{{"node_id", "Wait"}, {"x", 40}, {"y", 100}},
                                              J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}})},
                        {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
            {"execution", {{"time_limit_ms", 120000}}}};
        std::string revision;
        {
            wvd::app::Application writer({data, pack, {}, quests}, backend);
            auto created = call(writer, wvd::api::http::verb::post,
                                "/api/v1/workflows", document);
            revision = created.at("revision").get<std::string>();
        }
        wvd::app::Application application({data, pack, {}, quests}, backend);
        auto loaded = call(application, wvd::api::http::verb::get,
                           "/api/v1/workflows/offline-product-check");
        if (loaded.at("revision") != revision)
            throw std::runtime_error("PRODUCT_WORKFLOW_REOPEN_FAILED");
        J caller = document;
        caller["flow"]["id"] = "offline-product-caller";
        caller["flow"]["name"] = "Offline product caller";
        caller["entry"] = "Call";
        caller["nodes"] = J::array({
            J{{"id", "Call"}, {"type", "call"}, {"name", "Call"},
              {"parameters", {{"flow_id", "offline-product-check"}}}},
            J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
              {"parameters", {{"outcome", "success"}}}}});
        caller["edges"] = J::array({J{{"id", "call_done"}, {"from", "Call"},
            {"to", "Done"}, {"outcome", "success"}, {"order", 0}}});
        caller["layout"]["nodes"] = J::array({
            J{{"node_id", "Call"}, {"x", 40}, {"y", 100}},
            J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}});
        const auto created_caller = call(application, wvd::api::http::verb::post,
            "/api/v1/workflows", caller);
        wvd::api::Request deletion{wvd::api::http::verb::delete_,
            "/api/v1/workflows/offline-product-check", 11};
        deletion.body() = J{{"revision", revision}}.dump();
        deletion.prepare_payload();
        const auto rejected = application.handle(deletion);
        if (static_cast<unsigned>(rejected.status) < 400 ||
            rejected.body.find("WORKFLOW_STILL_REFERENCED") == std::string::npos)
            throw std::runtime_error("PRODUCT_REFERENCED_DELETE_NOT_REJECTED");
        deletion.target("/api/v1/workflows/offline-product-caller");
        deletion.body() = J{{"revision", created_caller.at("revision")}}.dump();
        deletion.prepare_payload();
        if (application.handle(deletion).status != wvd::api::http::status::no_content)
            throw std::runtime_error("PRODUCT_CALLER_DELETE_FAILED");
        auto accepted = call(application, wvd::api::http::verb::post,
            "/api/v1/workflows/offline-product-check/run",
            {{"revision", revision}, {"request_id", "offline-product-check-1"}});
        if (!accepted.value("accepted", false))
            throw std::runtime_error("PRODUCT_RUN_NOT_ACCEPTED");
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        J status;
        do {
            std::this_thread::sleep_for(50ms);
            status = call(application, wvd::api::http::verb::get, "/api/v1/runs/current");
            if (status.value("state", "") == "Completed" ||
                status.value("state", "") == "Failed" ||
                status.value("state", "") == "Interrupted") break;
        } while (std::chrono::steady_clock::now() < deadline);
        if (status.value("state", "") != "Completed") {
            J events = J::array();
            for (const auto &event : status.at("events").at("events"))
                events.push_back({{"type", event.value("type", "")},
                    {"node_id", event.value("node_id", J(nullptr))},
                    {"source_path", event.value("source_path", J(nullptr))}});
            throw std::runtime_error("PRODUCT_RUN_NOT_COMPLETED:" +
                status.value("reason", "") + ":" + events.dump());
        }
        if (status.value("completed_business_units", 0) != 1)
            throw std::runtime_error("PRODUCT_CHECKPOINT_MISSING");
        auto profile = call(application, wvd::api::http::verb::get, "/api/v1/profile");
        auto values = profile.at("profile");
        values["ACTIVE_BEG_MONEY"] = true;
        call(application, wvd::api::http::verb::put, "/api/v1/profile",
            {{"revision", profile.at("revision")}, {"profile", values}});
        auto task = call(application, wvd::api::http::verb::post,
            "/api/v1/runs/start",
            {{"task_id", "Scorpionesses"}, {"request_id", "offline-native-task-1"}});
        if (!task.value("accepted", false))
            throw std::runtime_error("PRODUCT_TASK_NOT_ACCEPTED");
        bool reached_native_task = false;
        const auto task_deadline = std::chrono::steady_clock::now() + 20s;
        do {
            std::this_thread::sleep_for(50ms);
            status = call(application, wvd::api::http::verb::get, "/api/v1/runs/current");
            for (const auto &event : status.at("events").at("events")) {
                if (event.value("type", "") == "step" && event.contains("node_id") &&
                    event.at("node_id").is_string() &&
                    event.at("node_id").get<std::string>().starts_with("Task_")) {
                    reached_native_task = true;
                    break;
                }
            }
            if (reached_native_task || status.value("state", "") == "Failed") break;
        } while (std::chrono::steady_clock::now() < task_deadline);
        call(application, wvd::api::http::verb::post, "/api/v1/runs/current/stop");
        if (!reached_native_task)
            throw std::runtime_error("PRODUCT_NATIVE_TASK_NOT_REACHED:" +
                status.value("reason", "") + ":" + status.value("error_code", J(nullptr)).dump());
        application.stop();
        std::cout << "Application persistence, native author run and money-handoff task entry passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
