#include "app/application.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/combat/debug.hpp"
#include "games/wvd/combat/strategy.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <thread>
#include <fstream>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace wvd::app {
// 仅测试替换磁盘查询依赖；生产 API 不暴露容量覆盖或离线开关。
struct ApplicationAssemblyTestAccess {
    static void space(Application &app, std::uintmax_t bytes, bool fail = false) {
        app.space_query_ = [bytes, fail](const auto &) -> std::filesystem::space_info {
            if (fail) throw std::runtime_error("fixture-space-query");
            return {bytes, bytes, bytes};
        };
    }
    static runtime::NativeRunCoordinator &coordinator(Application &app) { return *app.coordinator_; }
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
        app.watch_task_session({{"task_id", "Scorpionesses"}, {"repeat", true}, {"repeat_count", 2}},
            nlohmann::json::object(), nlohmann::json::object(), backend, id);
    }
    static nlohmann::json queue(Application &app, const std::string &id, int &prepared) {
        return app.queue_run("closure", {{"request_id", id}}, {{"fixture", "closure"}}, [&prepared] {
            ++prepared; return nlohmann::json{{"run_id", 0}};
        });
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
    program.revision = "closure"; program.root_definition = "root";
    workflow::Definition flow; flow.id = "root"; flow.entry = "checkpoint";
    const auto source = J::array({{{"flow_id", "closure"}, {"node_id", "checkpoint"}}}).dump();
    workflow::Step checkpoint; checkpoint.id = "checkpoint"; checkpoint.source_path = source;
    checkpoint.data = workflow::RegisteredOperation{"BusinessCheckpoint", J::object()}; checkpoint.next = {"finish"};
    workflow::Step finish; finish.id = "finish"; finish.source_path = J::array({{{"flow_id", "closure"}, {"node_id", "finish"}}}).dump(); finish.data = workflow::Finish{};
    flow.steps.emplace(checkpoint.id, checkpoint); flow.steps.emplace(finish.id, finish);
    program.definitions.emplace(flow.id, std::move(flow));
    runtime::NativeRunDefinition result; result.request_id = id;
    result.policy.device_id = "offline-product"; result.policy.game_id = "wvd";
    result.policy.application_id = "jp.co.drecom.wizardry.daphne";
    result.policy.pack_revision = "closure"; result.policy.viewport_id = "900x1600";
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
    A::space(app, 1073741824);
    const auto receipt = A::queue(app, "space-threshold", prepared);
    for (int i=0; i<200 && call(app, wvd::api::http::verb::get, "/api/v1/device").value("busy", false); ++i) std::this_thread::sleep_for(10ms);
    A::space(app, 0);
    const auto duplicate = A::queue(app, "space-threshold", prepared);
    require_closure(prepared == 1 && duplicate.at("request_id") == receipt.at("request_id"), "SPACE_IDEMPOTENCY_FAILED");
    std::cout << "SPACE-02 PASS: threshold admission and low-space existing receipt\n";
    for (const auto &fault : {std::string{"space"}, std::string{"timing"}, std::string{"terminal"}}) {
        auto isolated_backend = std::make_shared<OfflineConnection>(pack);
        wvd::app::Application isolated({root / fault, pack, {}, quests}, isolated_backend);
        auto &coordinator = A::coordinator(isolated);
        const auto inject_fault = [&] {
            if (fault == "space") return;
            const auto path = coordinator.run_directory() / (fault == "timing" ? "action-timing.jsonl" : "result.json");
            std::filesystem::create_directory(path);
        };
        coordinator.start(closure_definition(root / (fault + "-bundle"), fault, inject_fault), isolated_backend);
        require_closure(coordinator.wait_for(15s), "SPACE_FIXTURE_NOT_QUIESCENT");
        const auto terminal = coordinator.snapshot();
        if (fault == "space") require_closure(terminal.completed_business_units == 3 && terminal.result_saved, "SPACE_COMPLETED_SOURCE_REQUIRED:" + terminal.reason);
        if (fault == "timing") require_closure(std::find(terminal.secondary_errors.begin(), terminal.secondary_errors.end(), "ACTION_TIMING_INCOMPLETE") != terminal.secondary_errors.end(), "TIMING_FAILURE_UNREPORTED");
        if (fault == "terminal") require_closure(!terminal.result_saved && !terminal.storage_error.empty(), "TERMINAL_FAILURE_UNREPORTED");
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
        require_closure(status.at("repeat").at("completed_cycles") == (fault == "space" ? 1 : 0), "SPACE_COMPLETED_COUNT_LOST");
        if (fault == "space") require_closure(status.at("repeat").at("reason") == "RUN_STORAGE_SPACE_LOW", "SPACE_NEXT_REASON");
        std::ofstream(root / (fault + "-result.json")) << status.dump(2);
    }
    std::cout << "SPACE-03/04 PASS: completed cycle retained; storage/timing/terminal faults block next run\n";
    require_closure(wvd::platform::file_sha256(sentinel) == sentinel_hash, "SPACE_EXISTING_FILE_CHANGED");
    std::cout << "SPACE-05 PASS: existing sentinel unchanged; fixture root=" << root.string() << '\n';
    return 0;
}
}

int main(int argc, char **argv) {
    try {
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
