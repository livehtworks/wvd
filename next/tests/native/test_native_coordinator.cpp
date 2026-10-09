#include "runtime/native_run_coordinator.hpp"
#include "platform/windows/file_digest.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
#include <opencv2/imgcodecs.hpp>

namespace wvd::runtime {
struct NativeCoordinatorTestAccess {
    using CombatState = NativeRunCoordinator::CombatDiagnosticState;
    static void initialize(NativeRunCoordinator &coordinator, const nlohmann::json &definition,
        storage::DiagnosticLimits limits = {}, storage::LoggingPolicy logging = {},
        std::shared_ptr<const contracts::MonotonicClock> clock = std::make_shared<contracts::SteadyClock>()) {
        coordinator.snapshot_.run_id = 1;
        coordinator.snapshot_.reason = "ORIGINAL_BUSINESS_FAILURE";
        coordinator.store_ = std::make_unique<storage::RunStore>(coordinator.data_root_, coordinator.instance_id_, 1,
            definition, std::move(clock), limits, logging);
        coordinator.journal_ = std::make_shared<storage::EventJournal>(coordinator.instance_id_, 1, 256,
            [&coordinator](const auto &event) { coordinator.store_->append_event(event); });
    }
    static void save(NativeRunCoordinator &coordinator, const contracts::FrameEnvelope *frame) {
        coordinator.save_application_restart_diagnostic(1, 0, {{"step_id", "test-await"}}, frame);
    }
    static void combat(NativeRunCoordinator &coordinator, CombatState &state, const std::string &type,
        const nlohmann::json &data, const contracts::FrameEnvelope *frame) {
        coordinator.record_combat_diagnostic(1, 2, type, data, frame, state);
    }
    static nlohmann::json save(NativeRunCoordinator &coordinator, const contracts::FrameEnvelope *frame,
        const storage::DiagnosticRequest &request) {
        return coordinator.store_->save_diagnostic(frame, request);
    }
    static void complete(NativeRunCoordinator &coordinator) {
        auto snapshot = coordinator.snapshot_;
        snapshot.state = contracts::RunState::Completed;
        contracts::SessionResult session;
        session.end = contracts::SessionEnd::Completed;
        coordinator.store_->save_terminal(snapshot, session, coordinator.journal_->read());
    }
};
}

namespace {
using namespace wvd;
using namespace std::chrono_literals;
class State final : public contracts::BusinessRunState {
  protected:
    void on_segment(contracts::SegmentBoundary, std::uint64_t, std::size_t) override {}
    nlohmann::json summarize() const override { return {{"confirmed", true}}; }
};
class Backend : public devices::DeviceBackend {
  public:
    explicit Backend(bool release_ok = true) : release_ok_(release_ok) {}
    bool offline() const override { return true; }
    bool verified_access() const override { return true; }
    bool connect() override { return true; }
    devices::RawFrame capture() override {
        devices::RawFrame result;
        result.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
            900 * 1600 * 3, 0);
        result.size = {900, 1600};
        result.device_id = "native-test";
        result.viewport_id = "900x1600";
        result.foreground_application = "jp.co.drecom.wizardry.daphne";
        result.captured_at = std::chrono::steady_clock::now();
        result.capture_finished_at = result.captured_at;
        result.connection_generation = 1;
        result.display_rotation = 0;
        return result;
    }
    bool execute(const contracts::Command &) override {
        throw std::runtime_error("UNEXPECTED_TEST_INPUT");
    }
    bool release_owned_inputs() override { return release_ok_; }
  private:
    bool release_ok_;
};
class BlockingBackend final : public Backend {
  public:
    std::atomic<bool> entered{false};
    devices::RawFrame capture(std::stop_token stop) override {
        entered = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(5ms);
        throw std::runtime_error("CAPTURE_CANCELLED");
    }
};
}

int main(int argc, char **argv) {
    try {
        const auto data_root = std::filesystem::temp_directory_path() /
            ("wvd-native-coordinator-" + wvd::platform::unique_id());
        std::filesystem::create_directories(data_root);
        if (argc == 2 && std::string(argv[1]) == "--combat-diagnostic") {
            using A = runtime::NativeCoordinatorTestAccess;
            using J = nlohmann::json;
            const auto check = [](bool value, const char *code) {
                if (!value) throw std::runtime_error(code);
            };
            const J definition{{"engine_kind", "wvd_native"}, {"device_id", "native-test"},
                {"game_id", "wvd"}, {"pack_revision", "native-test"}, {"viewport", "900x1600"}};
            const J skill{{"role_var", "0 面具"}, {"skill_var", "左上技能"},
                {"skill_lvl", 2}, {"target_var", "next"}};
            const J prepare{{"operation", "prepare"}, {"generation", 1}, {"frame_id", 8},
                {"selection", {{"portrait", "0 面具"}, {"skill_index", 0}, {"strategy_epoch", 3},
                    {"strategy_name", "悬赏巨人"}, {"configured_skill", skill}}}};
            const std::string source = R"([{"flow_id":"combat-open-detail","node_id":"open"}])";
            const auto receipt = [&](std::uint64_t frame, std::uint64_t epoch) {
                return J{{"state", "result"}, {"source_path", source}, {"outcome", "no_progress"},
                    {"reason", "INPUT_RESULT_TIMEOUT"}, {"basis_frame", frame - 1},
                    {"observed_frame", frame}, {"action_epoch", epoch}, {"attempts", 9},
                    {"delivery_unknown", false}, {"result_wait_ns", 21000000000ULL}};
            };
            const auto pixels = [](std::uint64_t id, std::uint64_t epoch) {
                contracts::FrameEnvelope frame;
                frame.identity.device_id = "native-test"; frame.identity.game_id = "wvd";
                frame.identity.pack_revision = "native-test"; frame.identity.viewport_id = "900x1600";
                frame.identity.generation = frame.identity.connection_generation = 1;
                frame.identity.frame_id = id; frame.identity.action_epoch = epoch;
                frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
                frame.identity.captured_at = std::chrono::steady_clock::now();
                frame.identity.backend = "synthetic-diagnostic";
                frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(900 * 1600 * 3,
                    static_cast<std::uint8_t>(id));
                return frame;
            };
            const auto defended = [](std::uint64_t frame) {
                return J{{"operation", "defend_fallback_confirmed"}, {"generation", 1}, {"frame_id", frame},
                    {"action_confirmed", true}, {"skill_confirmed", false}, {"consumed", false}};
            };
            storage::LoggingPolicy logging;
            logging.level = storage::LogLevel::Off;
            logging.performance = false;
            {
                runtime::NativeRunCoordinator coordinator(data_root / "combat-pair");
                A::initialize(coordinator, definition, {}, logging);
                A::CombatState state;
                auto opening = pixels(10, 9), result = pixels(11, 10);
                A::combat(coordinator, state, "combat", prepare, &opening);
                A::combat(coordinator, state, "input", {{"state", "accepted"}, {"source_path", source},
                    {"action_epoch", 9}, {"basis_frame", 9}, {"position", {266, 965}}}, &opening);
                A::combat(coordinator, state, "input", receipt(10, 9), &opening);
                check(coordinator.diagnostics().at("entries").size() == 1,
                    "COMBAT_OPEN_FRAME_NOT_DURABLE_BEFORE_COMPLETION");
                A::combat(coordinator, state, "input", receipt(10, 9), &opening);
                A::combat(coordinator, state, "combat", defended(11), &result);
                A::combat(coordinator, state, "combat", defended(11), &result);
                auto summary = coordinator.diagnostics();
                const auto entries = summary.at("entries");
                check(entries.size() == 2 && entries.at(0).at("operation_id") == entries.at(1).at("operation_id") &&
                    entries.at(0).at("evidence_kind") == "combat_open_no_progress" &&
                    entries.at(1).at("evidence_kind") == "combat_defend_fallback" &&
                    entries.at(0).at("context").at("selection").at("configured_skill") == skill &&
                    entries.at(0).at("context").at("submission").at("position") == J::array({266, 965}) &&
                    entries.at(1).at("context").at("defend_result").at("skill_confirmed") == false &&
                    entries.at(1).at("context").at("defend_result").at("consumed") == false,
                    "COMBAT_PAIR_CONTEXT_OR_DEDUP");
                for (const auto &entry : entries) {
                    const auto path = coordinator.run_directory() / entry.at("path").get<std::string>();
                    const auto image = cv::imread(path.string());
                    const auto color = entry.at("frame").at("frame_id").get<std::uint8_t>();
                    check(entry.at("status") == "saved" && entry.at("unit_index") == 2 &&
                        entry.at("frame").at("input_authorization") == false && image.rows == 1600 && image.cols == 900 &&
                        image.at<cv::Vec3b>(0, 0) == cv::Vec3b{color, color, color} &&
                        platform::file_sha256(path) == entry.at("sha256").get<std::string>(), "COMBAT_PAIR_PIXELS_OR_OWNERSHIP");
                }
                // A second real action within 60 seconds has its own bounded evidence pair.
                opening = pixels(20, 19); result = pixels(21, 20);
                A::combat(coordinator, state, "combat", prepare, &opening);
                A::combat(coordinator, state, "input", receipt(20, 19), &opening);
                A::combat(coordinator, state, "combat", defended(21), &result);
                check(coordinator.diagnostics().at("entries").size() == 4 &&
                    coordinator.diagnostics().at("throttled") == 0 &&
                    coordinator.snapshot().reason == "ORIGINAL_BUSINESS_FAILURE", "COMBAT_DISTINCT_ACTION_ORIGINAL_RESULT");
                A::complete(coordinator);
                J saved;
                { std::ifstream file(coordinator.run_directory() / "result.json"); file >> saved; }
                check(saved.at("state") == "Completed" && saved.at("diagnostics").at("entries").size() == 4 &&
                    saved.at("diagnostics").at("event_history").at("rows") == 4 &&
                    saved.at("diagnostics").at("logs").at("rows") == 0,
                    "COMBAT_COMPLETED_OR_LOGGING_OFF_LOST_EVIDENCE");
                std::cout << "combat diagnostic: paired original PNGs, exact context, duplicate suppression, Completed and logging off\n";
            }
            {
                runtime::NativeRunCoordinator coordinator(data_root / "combat-ignore");
                A::initialize(coordinator, definition);
                A::CombatState state;
                auto frame = pixels(10, 9);
                A::combat(coordinator, state, "combat", prepare, &frame);
                for (const auto *mode : {"other-flow", "malformed", "unknown", "confirmed"}) {
                    auto input = receipt(10, 9);
                    if (std::string(mode) == "other-flow") input["source_path"] = R"([{"flow_id":"combat-open-detail-other","node_id":"open"}])";
                    if (std::string(mode) == "malformed") input["source_path"] = "combat-open-detail/open";
                    if (std::string(mode) == "unknown") input["delivery_unknown"] = true;
                    if (std::string(mode) == "confirmed") input["outcome"] = "confirmed";
                    A::combat(coordinator, state, "input", input, &frame);
                }
                A::combat(coordinator, state, "combat", defended(11), &frame);
                check(coordinator.diagnostics().at("entries").empty(), "COMBAT_UNRELATED_OR_UNKNOWN_CAPTURED");
                A::combat(coordinator, state, "input", receipt(10, 9), &frame);
                A::combat(coordinator, state, "combat", prepare, &frame);
                frame = pixels(11, 10);
                A::combat(coordinator, state, "combat", defended(11), &frame);
                check(coordinator.diagnostics().at("entries").size() == 1, "COMBAT_PREPARE_CARRIED_STALE_PAIR");
                std::cout << "combat diagnostic: unrelated, malformed, unknown delivery and stale action excluded\n";
            }
            for (const auto *mode : {"missing", "stale-generation", "wrong-frame", "wrong-epoch", "write-failed"}) {
                runtime::NativeRunCoordinator coordinator(data_root / mode);
                A::initialize(coordinator, definition);
                A::CombatState state;
                auto frame = pixels(10, 9);
                A::combat(coordinator, state, "combat", prepare, &frame);
                if (std::string(mode) == "stale-generation") frame.identity.generation = 2;
                if (std::string(mode) == "wrong-frame") frame.identity.frame_id = 9;
                if (std::string(mode) == "wrong-epoch") frame.identity.action_epoch = 8;
                if (std::string(mode) == "write-failed")
                    std::ofstream(coordinator.run_directory() / "diagnostics") << "isolated obstruction";
                A::combat(coordinator, state, "input", receipt(10, 9),
                    std::string(mode) == "missing" ? nullptr : &frame);
                const auto summary = coordinator.diagnostics();
                check(summary.at("entries").size() == 1 && summary.at("entries").at(0).at("status") == "failed" &&
                    !summary.at("entries").at(0).contains("path") && !summary.at("complete").get<bool>() &&
                    coordinator.snapshot().reason == "ORIGINAL_BUSINESS_FAILURE", "COMBAT_MISSING_FRAME_OR_SAVE_FAILURE_HIDDEN");
            }
            {
                runtime::NativeRunCoordinator coordinator(data_root / "combat-reconnected");
                A::initialize(coordinator, definition);
                A::CombatState state;
                auto frame = pixels(10, 9);
                A::combat(coordinator, state, "combat", prepare, &frame);
                A::combat(coordinator, state, "input", receipt(10, 9), &frame);
                frame = pixels(11, 10); frame.identity.connection_generation = 2;
                A::combat(coordinator, state, "combat", defended(11), &frame);
                const auto entry = coordinator.diagnostics().at("entries").at(1);
                check(entry.at("status") == "failed" && !entry.at("context").at("frame_available").get<bool>(),
                    "COMBAT_CROSS_CONNECTION_FRAME_PAIRED");
            }
            {
                runtime::NativeRunCoordinator coordinator(data_root / "combat-opening-unavailable");
                A::initialize(coordinator, definition);
                A::CombatState state;
                A::combat(coordinator, state, "combat", prepare, nullptr);
                A::combat(coordinator, state, "input", receipt(10, 9), nullptr);
                auto frame = pixels(11, 10);
                A::combat(coordinator, state, "combat", defended(11), &frame);
                const auto summary = coordinator.diagnostics();
                const auto entry = summary.at("entries").at(1);
                check(summary.at("entries").at(0).at("status") == "failed" && entry.at("status") == "saved" &&
                    entry.at("context").at("opening_frame_available") == false &&
                    entry.at("context").at("same_connection_as_opening").is_null() &&
                    !summary.at("complete").get<bool>(), "COMBAT_MISSING_OPENING_IDENTITY_ASSUMED");
            }
            {
                struct Clock final : contracts::MonotonicClock {
                    TimePoint value = std::chrono::steady_clock::now();
                    TimePoint now() const noexcept override { return value; }
                };
                auto clock = std::make_shared<Clock>();
                runtime::NativeRunCoordinator coordinator(data_root / "combat-quota");
                storage::DiagnosticLimits limits; limits.failures = 2;
                A::initialize(coordinator, definition, limits, {}, clock);
                auto frame = pixels(10, 9);
                storage::DiagnosticRequest request;
                request.run_id = request.generation = 1; request.node = "combat-open-detail/open";
                request.reason = "combat.open_detail_no_progress"; request.stage = "postcondition";
                request.evidence_kind = "combat_open_no_progress";
                request.operation_scoped = true; request.operation_id = "same-action";
                check(A::save(coordinator, &frame, request).at("status") == "saved", "COMBAT_QUOTA_FIRST");
                clock->value += 120s;
                check(A::save(coordinator, &frame, request).at("status") == "duplicate", "COMBAT_DUPLICATE_AFTER_INTERVAL");
                request.evidence_kind = "combat_defend_fallback";
                check(A::save(coordinator, &frame, request).at("status") == "saved", "COMBAT_QUOTA_PAIR");
                request.operation_id = "next-action";
                check(A::save(coordinator, &frame, request).at("status") == "quota_exceeded" &&
                    coordinator.diagnostics().at("failure_attempts") == 2 &&
                    !coordinator.diagnostics().at("complete").get<bool>(), "COMBAT_FAILURE_QUOTA_BYPASSED");
                request.context = J::array();
                check(A::save(coordinator, &frame, request).at("status") == "invalid_request", "COMBAT_CONTEXT_TYPE_UNBOUNDED");
                request.context = {{"oversize", std::string(16 * 1024, 'x')}};
                check(A::save(coordinator, &frame, request).at("status") == "invalid_request", "COMBAT_CONTEXT_BYTES_UNBOUNDED");
            }
            std::cout << "combat diagnostic: unavailable/write errors remain visible, connection identity and original quotas preserved\n";
            std::cout << "Evidence: " << data_root.string() << '\n';
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--event-history") {
            storage::LoggingPolicy policy;
            policy.level = storage::LogLevel::Off;
            storage::RunStore store(data_root, "history", 1, {{"kind", "isolated"}},
                std::make_shared<contracts::SteadyClock>(), {}, policy);
            storage::EventJournal journal("history", 1, 8,
                [&](const auto &event) { store.append_event(event); });
            for (int i = 0; i < 20; ++i) journal.emit(1, "business_combat", {{"index", i}});
            if (!journal.read().at("resync_required").get<bool>()) throw std::runtime_error("HISTORY_RING_NOT_TRUNCATED");
            const auto summary = store.diagnostic_summary().at("event_history");
            if (summary.at("rows") != 20 || !summary.at("complete").get<bool>())
                throw std::runtime_error("HISTORY_EARLY_EVENTS_LOST");
            std::ifstream stream(store.directory() / "execution-events.jsonl");
            std::string line;
            int expected = 1;
            while (std::getline(stream, line)) {
                const auto row = nlohmann::json::parse(line);
                if (row.at("seq") != expected || row.at("payload").at("index") != expected - 1)
                    throw std::runtime_error("HISTORY_SEQUENCE_INVALID");
                ++expected;
            }
            if (expected != 21) throw std::runtime_error("HISTORY_FILE_INCOMPLETE");
            store.record_memory_boundary("worker_joined", {{"private_bytes", 123}});
            if (std::filesystem::exists(store.directory() / "memory-lifecycle.json"))
                throw std::runtime_error("HISTORY_MEMORY_OFF_IGNORED");
            storage::RunStore failed(data_root, "history", 2, {{"kind", "isolated"}});
            std::filesystem::create_directory(failed.directory() / "execution-events.jsonl");
            failed.append_event({{"seq", 1}});
            if (failed.diagnostic_summary().at("complete").get<bool>() ||
                failed.diagnostic_summary().at("event_history").at("failed") != 1)
                throw std::runtime_error("HISTORY_WRITE_FAILURE_HIDDEN");
            std::cout << "Event history survives ring eviction and logging off; write failure reported\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--logging-policy") {
            using namespace wvd::storage;
            auto policy = LoggingPolicy{};
            policy.performance = false;
            policy.memory = false;
            policy.recognition = false;
            const auto parsed = LoggingPolicy::parse(policy.json());
            if (parsed.performance || parsed.memory || parsed.recognition ||
                parsed.memory_interval_ms != 1000)
                throw std::runtime_error("LOGGING_POLICY_ROUNDTRIP");
            auto invalid = policy.json();
            invalid["memory_interval_ms"] = 10;
            bool rejected = false;
            try { (void)LoggingPolicy::parse(invalid); } catch (const std::runtime_error &) { rejected = true; }
            if (!rejected) throw std::runtime_error("LOGGING_POLICY_INTERVAL_ACCEPTED");
            RunStore store(data_root, "logging-off", 1, {{"kind", "isolated"}},
                std::make_shared<contracts::SteadyClock>(), {}, policy);
            store.append_timing(1, "timing.segment", {{"node_id", "test"}});
            store.append_timing(1, "input.attempt", {{"state", "accepted"}});
            store.append_log(1, LogLevel::Info, "memory", "before_session", {{"private_bytes", 1}});
            store.append_log(1, LogLevel::Warn, "runtime", "session_incomplete", {{"code", "test"}});
            const auto summary = store.diagnostic_summary();
            if (summary.at("action_timing").at("rows") != 1 ||
                summary.at("action_timing").at("collected") != false ||
                summary.at("logs").at("rows") != 1 ||
                summary.at("logs").at("memory_collected") != false ||
                !summary.at("complete").get<bool>())
                throw std::runtime_error("LOGGING_POLICY_FILTER_FAILED");
            nlohmann::json input, warning;
            { std::ifstream file(store.directory() / "action-timing.jsonl"); file >> input; }
            { std::ifstream file(store.directory() / "diagnostics.jsonl"); file >> warning; }
            if (input.at("type") != "input.attempt" || input.at("category") != "input_audit" ||
                warning.at("type") != "session_incomplete" || warning.at("level") != "warn")
                throw std::runtime_error("LOGGING_POLICY_AUDIT_FAILED");
            EventJournal journal("logging-test", 1);
            journal.emit(1, "input.result", {{"outcome", "confirmed"}});
            const auto event = journal.read().at("events").at(0);
            if (event.at("category") != "input_audit" || event.at("level") != "info")
                throw std::runtime_error("LOGGING_EVENT_CLASSIFICATION_FAILED");
            policy.level = LogLevel::Off;
            RunStore disabled(data_root, "logging-off", 2, {{"kind", "isolated"}},
                std::make_shared<contracts::SteadyClock>(), {}, policy);
            disabled.append_timing(1, "input.result", {{"state", "confirmed"}});
            disabled.append_log(1, LogLevel::Error, "runtime", "optional_failure", {});
            if (disabled.diagnostic_summary().at("action_timing").at("rows") != 1 ||
                disabled.diagnostic_summary().at("logs").at("rows") != 0 ||
                LoggingPolicy::parse(policy.json()).accepts(LogLevel::Error))
                throw std::runtime_error("LOGGING_OFF_AUDIT_FAILED");
            std::cout << "Logging policy, mandatory audit and categories passed\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--restart-diagnostic") {
            using A = runtime::NativeCoordinatorTestAccess;
            for (const auto *mode : {"saved", "missing", "write-failed"}) {
                runtime::NativeRunCoordinator coordinator(data_root / mode);
                A::initialize(coordinator, {{"engine_kind", "wvd_native"}, {"device_id", "native-test"},
                    {"game_id", "wvd"}, {"pack_revision", "native-test"}, {"viewport", "900x1600"}});
                contracts::FrameEnvelope frame;
                frame.identity.device_id = "native-test"; frame.identity.game_id = "wvd";
                frame.identity.pack_revision = "native-test"; frame.identity.viewport_id = "900x1600";
                frame.identity.generation = frame.identity.frame_id = frame.identity.connection_generation = 1;
                frame.identity.raw_size = frame.identity.recognition_size = {900,1600};
                frame.identity.captured_at = std::chrono::steady_clock::now();
                frame.identity.backend = "synthetic-diagnostic";
                frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(900 * 1600 * 3, 123);
                if (std::string(mode) == "write-failed")
                    std::ofstream(coordinator.run_directory() / "diagnostics") << "isolated obstruction";
                A::save(coordinator, std::string(mode) == "missing" ? nullptr : &frame);
                const auto summary = coordinator.diagnostics();
                const auto &entry = summary.at("entries").at(0);
                const bool saved = std::string(mode) == "saved";
                if (entry.at("stage") != "recovery_entry" || entry.at("evidence_kind") != "before_application_restart" ||
                    entry.at("reason") != "CONTINUOUS_EXCEPTION_TIMEOUT" || entry.at("generation") != 1 ||
                    entry.at("status") != (saved ? "saved" : "failed") || summary.at("complete") != saved ||
                    coordinator.snapshot().reason != "ORIGINAL_BUSINESS_FAILURE")
                    throw std::runtime_error("RESTART_DIAGNOSTIC_CONTRACT");
                if (saved) {
                    const auto path = coordinator.run_directory() / entry.at("path").get<std::string>();
                    const auto image = cv::imread(path.string());
                    if (image.rows != 1600 || image.cols != 900 || image.at<cv::Vec3b>(0,0) != cv::Vec3b{123,123,123} ||
                        platform::file_sha256(path) != entry.at("sha256").get<std::string>() || entry.at("frame").at("frame_id") != 1)
                        throw std::runtime_error("RESTART_DIAGNOSTIC_PIXELS");
                }
                std::cout << "restart diagnostic " << mode << " complete=" << summary.at("complete") << '\n';
                std::ofstream(coordinator.run_directory() / "hook-result.json") << summary.dump(2);
            }
            std::cout << "Evidence: " << data_root.string() << '\n';
            return 0;
        }
        const auto bundle_root = data_root / "bundle";
        std::filesystem::create_directories(bundle_root);
        { std::ofstream marker(bundle_root / "marker.txt", std::ios::binary);
          marker << "native bundle"; }
        recognition::Bundle bundle{bundle_root, "native-test",
            {{"marker.txt", platform::file_sha256(bundle_root / "marker.txt")}}};
        workflow::FlowProgram program;
        program.revision = "native-test";
        program.root_definition = "root";
        workflow::Definition root;
        root.id = "root";
        root.entry = "checkpoint";
        workflow::Step checkpoint;
        checkpoint.id = "checkpoint";
        const std::string checkpoint_path = R"([{"definition":"root","native_node":"checkpoint"}])";
        checkpoint.source_path = checkpoint_path;
        checkpoint.data = workflow::RegisteredOperation{"BusinessCheckpoint",
            nlohmann::json::object()};
        checkpoint.next = {"finish"};
        root.steps.emplace("checkpoint", std::move(checkpoint));
        workflow::Step finish;
        finish.id = "finish";
        finish.source_path = R"([{"definition":"root","native_node":"finish"}])";
        finish.data = workflow::Finish{};
        root.steps.emplace("finish", std::move(finish));
        program.definitions.emplace("root", std::move(root));
        runtime::NativeRunDefinition definition;
        definition.request_id = "native-coordinator-test";
        definition.policy.device_id = "native-test";
        definition.policy.game_id = "wvd";
        definition.policy.application_id = "jp.co.drecom.wizardry.daphne";
        definition.policy.pack_revision = "native-test";
        definition.policy.viewport_id = "900x1600";
        definition.policy.recognition_size = {900, 1600};
        definition.policy.allowed_scenes.insert("wvd");
        if (argc == 2 && std::string(argv[1]) == "--capture-context") {
            class ContextBackend final : public Backend {
              public:
                bool desktop = true;
                devices::RawFrame capture() override {
                    auto result = Backend::capture();
                    if (desktop) {
                        result.foreground_application = "com.android.launcher";
                        result.size = {1600, 900}; result.viewport_id = "1600x900";
                        result.display_rotation = 1;
                    }
                    return result;
                }
            } source;
            recognition::Service recognizer(bundle, {});
            State business;
            runtime::NativeFlowPorts ports(source, recognizer, business, definition.policy, 1, {});
            try { (void)ports.capture(); throw std::runtime_error("DESKTOP_REACHED_GAME_ROI"); }
            catch (const contracts::ObservationUnavailable &error) {
                if (error.fault().code != "GAME_NOT_FOREGROUND" ||
                    !ports.failed_pixels() || ports.failed_pixels()->size != contracts::Size{1600,900} ||
                    ports.last_valid_frame()) throw;
            }
            storage::RunStore foreground_store(data_root / "foreground", "native-test", 1,
                {{"engine_kind", "wvd_native"}, {"device_id", "native-test"},
                 {"game_id", "wvd"}, {"pack_revision", "native-test"}, {"viewport", "900x1600"}});
            storage::DiagnosticRequest request;
            request.run_id = request.generation = 1; request.node = "capture.foreground";
            request.reason = "GAME_NOT_FOREGROUND"; request.stage = "recovery_entry";
            request.evidence_kind = "foreground_lost_pixels";
            const auto pixels = ports.failed_pixels();
            const auto saved = foreground_store.save_diagnostic(nullptr, request, &*pixels);
            if (saved.at("status") != "saved" || !saved.at("metadata_valid").get<bool>() ||
                saved.at("pixels").at("input_authorization") != false)
                throw std::runtime_error("FOREGROUND_PIXELS_NOT_SAVED");
            const auto decoded = cv::imread((foreground_store.directory() / saved.at("path").get<std::string>()).string());
            if (decoded.cols != 1600 || decoded.rows != 900)
                throw std::runtime_error("FOREGROUND_PIXELS_VIEWPORT_CHANGED");
            request.node = "wrong-node";
            storage::RunStore rejected_store(data_root / "rejected-foreground", "native-test", 1,
                {{"engine_kind", "wvd_native"}, {"device_id", "native-test"},
                 {"game_id", "wvd"}, {"pack_revision", "native-test"}, {"viewport", "900x1600"}});
            if (rejected_store.save_diagnostic(nullptr, request, &*pixels).at("status") != "failed")
                throw std::runtime_error("ARBITRARY_PIXELS_KIND_ACCEPTED");
            source.desktop = false;
            const auto frame = ports.capture();
            if (frame.identity.recognition_size != contracts::Size{900,1600} || ports.failed_pixels())
                throw std::runtime_error("GAME_FRAME_NOT_RESTORED");
            auto read_policy = definition.policy; read_policy.observed_read_only_viewport = true;
            runtime::NativeFlowPorts preview(source, recognizer, business, read_policy, 1, {});
            source.desktop = true;
            if (preview.capture().identity.recognition_size != contracts::Size{1600,900})
                throw std::runtime_error("READ_ONLY_PREVIEW_BLOCKED");
            std::cout << "capture context: desktop rejected before ROI; game restored; read-only preview preserved\n";
            return 0;
        }
        definition.units.push_back({std::make_shared<const workflow::FlowProgram>(std::move(program)), std::move(bundle), {},
            checkpoint_path, 5s});
        definition.total_time_limit = 5s;
        definition.create_state = [](const auto &) { return std::make_unique<State>(); };
        definition.operations = [](auto &, auto, auto checkpoint_callback) {
            return [checkpoint_callback](runtime::NativeFlowPorts &) {
                return [checkpoint_callback](const std::string &binding, const auto &,
                    const auto &, const auto &, const std::string &source_path) {
                    if (binding != "BusinessCheckpoint")
                        return runtime::OperationResult{runtime::OperationState::Failed,
                            "UNEXPECTED_OPERATION"};
                    checkpoint_callback(source_path);
                    return runtime::OperationResult{runtime::OperationState::Done};
                };
            };
        };
        if (argc == 2 && std::string(argv[1]) == "--instance-exit") {
            class ExitBackend final : public Backend, public devices::LifecyclePort {
              public:
                int submissions{}, restarts{};
                bool restored{}, exited{}, read_recovery{};
                std::uint64_t connection{1};
                devices::LifecycleTarget target{"native-test", "2", "jp.co.drecom.wizardry.daphne", "", false};
                bool offline() const override { return false; }
                bool context_matches(const contracts::FrameIdentity &, const std::string &) override { return !exited; }
                bool input_channel_ready() const override { return true; }
                void prepare_input_channel(std::stop_token) override {}
                bool execute(const contracts::Command &) override { ++submissions; exited = true; return true; }
                devices::RawFrame capture() override {
                    if (exited && read_recovery)
                        throw contracts::ObservationUnavailable({contracts::ReadFaultKind::TransportUnavailable,
                            contracts::ReadFaultStage::Capture, "ADB_TRANSPORT_FAILED", "fixture.capture", {}, {}});
                    if (exited) throw std::runtime_error("MUMU_INSTANCE_MISMATCH");
                    auto frame = Backend::capture(); frame.connection_generation = connection; return frame;
                }
                devices::LifecyclePort *lifecycle_port() override { return this; }
                contracts::ObservationRecovery recover_observation(bool = false) override {
                    if (!read_recovery) return {};
                    ++restarts; exited = false; restored = true; ++connection;
                    return {contracts::ObservationReconnect{target.device_id, target.instance_id, "fixture-instance", 1, connection},
                        true, true, true};
                }
                std::optional<devices::LifecycleObservation> observe_lifecycle() override {
                    return devices::LifecycleObservation{target, !exited, !exited, restored, true,
                        connection, std::chrono::steady_clock::now(), restored, exited};
                }
                bool execute_lifecycle(devices::LifecycleOperation op, const devices::LifecycleTarget &,
                    const std::function<bool()> &) override {
                    if (op == devices::LifecycleOperation::RestartInstance) {
                        ++restarts; exited = false; ++connection;
                    } else if (op == devices::LifecycleOperation::StartApplication) restored = true;
                    return true;
                }
            };
            for (const std::string mode : {"protected", "exit", "read-recovery"}) {
                const bool discardable = mode != "protected";
                auto source = std::make_shared<ExitBackend>();
                source->read_recovery = mode == "read-recovery";
                auto graph = std::make_shared<workflow::FlowProgram>(*definition.units.front().program);
                auto &root = graph->definitions.at("root"); root.entry = "dispatch";
                recognition::Request probe{"wvd", "1", {0,0,900,1600},
                    recognition::CustomParameters{"ExitFixture", nlohmann::json::object()}};
                auto ready = probe; std::get<recognition::CustomParameters>(ready.parameters).parameters["ready"] = true;
                workflow::Step dispatch; dispatch.id = "dispatch"; dispatch.source_path = R"([{"native_node":"dispatch"}])";
                dispatch.data = workflow::Route{}; dispatch.next = {"ready", "skill"};
                root.steps.emplace(dispatch.id, std::move(dispatch));
                workflow::Step found; found.id = "ready"; found.source_path = R"([{"native_node":"ready"}])";
                found.data = workflow::Observe{ready}; found.guard = ready; found.next = {"checkpoint"};
                root.steps.emplace(found.id, std::move(found));
                workflow::Input input{probe, probe, {{"kind", "Click"}}, {0,0,900,1600}};
                input.interruption_reason = "combat.skill_outcome_unconfirmed";
                input.instance_exit_discardable = discardable;
                workflow::Step skill; skill.id = "skill"; skill.source_path = R"([{"native_node":"skill"}])";
                skill.data = std::move(input); skill.next = {"await"};
                root.steps.emplace(skill.id, std::move(skill));
                workflow::Step await; await.id = "await"; await.source_path = R"([{"native_node":"skill"}])";
                await.data = workflow::AwaitResult{ready, 1s}; await.next = {"checkpoint"};
                root.steps.emplace(await.id, std::move(await));
                runtime::NativeRunDefinition test;
                test.request_id = mode;
                test.policy = definition.policy;
                test.policy.capabilities.insert(contracts::ActionKind::Click);
                test.policy.permissions.insert(contracts::ActionKind::Click);
                test.total_time_limit = 5s; test.create_state = definition.create_state; test.operations = definition.operations;
                auto unit = definition.units.front(); unit.program = graph;
                unit.recognizers["ExitFixture"] = [source](const auto &, auto, const nlohmann::json &p, const auto &, auto &) {
                    return nlohmann::json{{"schema",1}, {"outcome", p.value("ready",false) && !source->restored ? "NoHit" : "Hit"},
                        {"box", {100,100,20,20}}, {"target",true}};
                };
                test.units.push_back(std::move(unit));
                test.recovery = [source](const auto &result, const auto &, unsigned) -> std::optional<devices::LifecyclePlan> {
                    if (result.reason != (source->read_recovery ? "device.instance_restarted" : "device.instance_exited"))
                        throw std::runtime_error("EXIT_NOT_OBSERVED");
                    devices::LifecyclePlan plan{source->target, {devices::LifecycleOperation::StartApplication}, 1};
                    if (!source->read_recovery) plan.operations.insert(plan.operations.begin(), devices::LifecycleOperation::RestartInstance);
                    return plan;
                };
                runtime::NativeRunCoordinator run(data_root / test.request_id);
                run.start(std::move(test), source);
                if (!run.wait_for(5s)) throw std::runtime_error("EXIT_RECOVERY_NOT_TERMINAL");
                const auto ended = run.snapshot();
                if (source->submissions != 1 || source->restarts != (discardable ? 1 : 0) ||
                    ended.state != (discardable ? contracts::RunState::Completed : contracts::RunState::Failed) ||
                    ended.sessions.at(0).at("unresolved_inputs").size() != 1 ||
                    (discardable && !ended.unresolved_inputs.empty()))
                    throw std::runtime_error("EXIT_RECOVERY_REPLAY_OR_HISTORY_LOST:" + ended.reason);
                bool interrupted = false;
                const auto recorded = run.events();
                for (const auto &event : recorded.at("events"))
                    if (event.at("type") == "recovery.input_interrupted") interrupted = true;
                if (interrupted != discardable) throw std::runtime_error("EXIT_AUDIT_MISSING");
            }
            std::cout << "instance exit: session-local skill resumed without replay; protected input preserved\n";
            return 0;
        }
        runtime::NativeRunCoordinator coordinator(data_root / "runs");
        auto backend = std::make_shared<Backend>();
        const auto fixture = [&] {
            runtime::NativeRunDefinition value;
            value.policy = definition.policy;
            value.units = definition.units; // 不可变程序共享；修改场景另行显式封存。
            value.total_time_limit = definition.total_time_limit;
            value.create_state = definition.create_state;
            value.operations = definition.operations;
            return value;
        };
        auto pending_definition = fixture();
        auto deferred_definition = fixture();
        auto capture_definition = fixture();
        pending_definition.request_id = "native-cleanup-pending";
        const auto started = coordinator.start(std::move(definition), backend);
        if (!started.run_id || !coordinator.wait_for(5s))
            throw std::runtime_error("COORDINATOR_NOT_FINISHED");
        const auto ended = coordinator.snapshot();
        if (ended.state != contracts::RunState::Completed || !ended.quiescent ||
            !ended.result_saved || ended.completed_business_units != 1)
            throw std::runtime_error("COORDINATOR_TERMINAL_INVALID:" + ended.reason +
                ":" + ended.storage_error);
        if (argc == 2 && std::string(argv[1]) == "--logging-boundary") {
            const std::vector<unsigned char> live_allocation(65536, 0x5a);
            if (!coordinator.collect_finished_worker()) throw std::runtime_error("MEMORY_WORKER_NOT_JOINED");
            if (!std::all_of(live_allocation.begin(), live_allocation.end(), [](auto value) { return value == 0x5a; }))
                throw std::runtime_error("MEMORY_HEAP_MAINTENANCE_CHANGED_LIVE_ALLOCATION");
            coordinator.record_batch_release();
            nlohmann::json boundaries;
            { std::ifstream file(coordinator.run_directory() / "memory-lifecycle.json"); file >> boundaries; }
            for (const auto *phase : {"worker_definition_released", "worker_joined", "heap_resources_optimized", "batch_payloads_released"}) {
                if (!boundaries.at("samples").at(phase).at("process_memory_available").get<bool>())
                    throw std::runtime_error("MEMORY_POST_WORKER_SAMPLE_MISSING");
            }
            if (boundaries.at("failed") != 0 || boundaries.at("run_id") != ended.run_id)
                throw std::runtime_error("MEMORY_POST_WORKER_IDENTITY");
            const auto &maintenance = boundaries.at("samples").at("heap_resources_optimized").at("heap_maintenance");
            if (!maintenance.at("succeeded").get<bool>() || maintenance.at("win32_error") != 0 ||
                !maintenance.at("before_process_ok").get<bool>())
                throw std::runtime_error("MEMORY_HEAP_MAINTENANCE_FAILED");
            for (const auto *name : {"heap_summary_before", "heap_summary_after"}) {
                const auto &heap = maintenance.at(name);
                const bool supported = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "HeapSummary") != nullptr;
                if (heap.at("available").get<bool>() != supported ||
                    (supported && (!heap.at("complete").get<bool>() || heap.at("failed") != 0 ||
                        heap.at("heaps").get<unsigned>() == 0 || heap.at("allocated_bytes").get<std::uint64_t>() < live_allocation.size())))
                    throw std::runtime_error("MEMORY_HEAP_SUMMARY_INVALID");
                std::uint64_t allocated = 0;
                for (const auto &entry : heap.at("per_heap")) {
                    if (!entry.at("complete").get<bool>() || entry.at("address") == 0)
                        throw std::runtime_error("MEMORY_PER_HEAP_INCOMPLETE");
                    allocated += entry.at("allocated_bytes").get<std::uint64_t>();
                }
                if (allocated != heap.at("allocated_bytes").get<std::uint64_t>())
                    throw std::runtime_error("MEMORY_PER_HEAP_TOTAL_MISMATCH");
            }
            const auto silent = platform::optimize_idle_heap(false);
            if (!silent.succeeded || silent.heap_before.available || silent.heap_after.available)
                throw std::runtime_error("MEMORY_DISABLED_STILL_ENUMERATES_HEAPS");
            coordinator.collect_finished_worker();
            coordinator.record_batch_release();
            nlohmann::json repeated;
            { std::ifstream file(coordinator.run_directory() / "memory-lifecycle.json"); file >> repeated; }
            if (repeated.at("samples").at("heap_resources_optimized") != boundaries.at("samples").at("heap_resources_optimized"))
                throw std::runtime_error("MEMORY_HEAP_MAINTENANCE_REPEATED_WITHOUT_WORKER");
            nlohmann::json saved;
            { std::ifstream file(coordinator.run_directory() / "result.json"); file >> saved; }
            std::ifstream history(coordinator.run_directory() / "execution-events.jsonl");
            std::string event_line;
            std::uint64_t sequence = 0;
            while (std::getline(history, event_line)) {
                const auto event = nlohmann::json::parse(event_line);
                if (event.at("seq") != ++sequence || event.at("run_id") != ended.run_id)
                    throw std::runtime_error("COORDINATOR_HISTORY_SEQUENCE");
            }
            if (!sequence || saved.at("diagnostics").at("event_history").at("rows") != sequence ||
                saved.at("events").at("last_seq") != sequence + 1)
                throw std::runtime_error("COORDINATOR_HISTORY_NOT_CONNECTED");
            std::ifstream log(coordinator.run_directory() / "diagnostics.jsonl");
            std::vector<std::string> phases;
            std::string line;
            while (std::getline(log, line)) {
                const auto row = nlohmann::json::parse(line);
                if (row.at("category") == "memory") {
                    phases.push_back(row.at("type").get<std::string>());
                    if (phases.back() == "runtime_sample" || phases.back() == "system_pressure") {
                        const auto &sample = row.at("payload");
                        const auto limit = sample.at("system_commit_limit_bytes").get<std::uint64_t>();
                        const bool pressure = sample.at("system_memory_available").get<bool>() && limit &&
                            static_cast<double>(sample.at("system_commit_bytes").get<std::uint64_t>()) / limit >= .90;
                        if (phases.back() != (pressure ? "system_pressure" : "runtime_sample") ||
                            row.at("level") != (pressure ? "warn" : "info"))
                            throw std::runtime_error("LOGGING_MEMORY_PRESSURE_CLASSIFICATION");
                        phases.back() = "runtime_sample";
                    }
                    const auto &owners = row.at("payload").at("object_lifetimes");
                    if (phases.back() == "session_owners_alive" &&
                        (owners.at("recognizers").at("live") == 0 || owners.at("execution_sessions").at("live") == 0 ||
                         !row.at("payload").contains("recognizer_ownership") || !row.at("payload").contains("execution_ownership")))
                        throw std::runtime_error("LOGGING_OWNERSHIP_CENSUS_MISSING");
                    if (phases.back() == "session_owners_released" &&
                        (owners.at("recognizers").at("live") != 0 || owners.at("execution_sessions").at("live") != 0))
                        throw std::runtime_error("LOGGING_OWNERSHIP_LIFETIMES_NOT_ZERO");
                    if (phases.back() == "session_owners_released" &&
                        (!row.at("payload").at("recognizer_released").get<bool>() ||
                         !row.at("payload").at("session_released").get<bool>()))
                        throw std::runtime_error("LOGGING_OWNERS_STILL_ALIVE");
                }
            }
            if (phases != std::vector<std::string>{"before_session", "runtime_sample", "session_owners_alive",
                    "session_owners_released", "worker_finishing"})
                throw std::runtime_error("LOGGING_MEMORY_BOUNDARY_ORDER");
            const auto summary = coordinator.diagnostics();
            if (summary.at("logs").at("rows") != 5 ||
                !summary.at("logs").at("memory_collected").get<bool>())
                throw std::runtime_error("LOGGING_MEMORY_SUMMARY");
            std::cout << "Memory owner boundaries passed\n";
            return 0;
        }
        {
            runtime::NativeRunCoordinator pending(data_root / "pending-runs");
            auto unreleased = std::make_shared<Backend>(false);
            pending.start(std::move(pending_definition), unreleased);
            if (!pending.wait_for_worker(5s) || pending.wait_for(0ms))
                throw std::runtime_error("CLEANUP_PENDING_WAIT_SEMANTICS_INVALID");
            const auto state = pending.snapshot();
            if (state.state != contracts::RunState::Interrupted || state.quiescent)
                throw std::runtime_error("CLEANUP_PENDING_TERMINAL_INVALID");
        }
        {
            auto adjusted = std::make_shared<workflow::FlowProgram>(*deferred_definition.units.front().program);
            auto &root = adjusted->definitions.at("root");
            root.steps.at("checkpoint").data = workflow::Fail{"deferred.recovery"};
            deferred_definition.units.front().program = std::move(adjusted);
            deferred_definition.request_id = "native-deferred-stop";
            deferred_definition.total_time_limit = 10s;
            deferred_definition.recovery = [](const contracts::SessionResult &result,
                const contracts::BusinessRunState &, unsigned attempt)
                -> std::optional<devices::LifecyclePlan> {
                if (result.reason != "deferred.recovery" || attempt != 1)
                    throw std::runtime_error("DEFERRED_RECOVERY_NOT_REACHED");
                devices::LifecyclePlan plan;
                plan.target = {"native-test", "native-instance",
                    "jp.co.drecom.wizardry.daphne", "", false};
                plan.operations = {devices::LifecycleOperation::StopApplication,
                    devices::LifecycleOperation::StartApplication};
                plan.attempt = 1;
                plan.defer_for = 5s;
                return plan;
            };
            runtime::NativeRunCoordinator deferred(data_root / "deferred-runs");
            deferred.start(std::move(deferred_definition), backend);
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            bool waiting = false;
            while (std::chrono::steady_clock::now() < deadline) {
                for (const auto &entry : deferred.events().value("events", nlohmann::json::array()))
                    if (entry.value("type", "") == "recovery.deferred") waiting = true;
                if (waiting) break;
                std::this_thread::sleep_for(10ms);
            }
            if (!waiting) throw std::runtime_error("DEFERRED_RECOVERY_NOT_WAITING");
            const auto stop_started = std::chrono::steady_clock::now();
            deferred.request_stop();
            if (!deferred.wait_for(500ms) ||
                std::chrono::steady_clock::now() - stop_started > 500ms ||
                deferred.snapshot().state != contracts::RunState::UserStopped)
                throw std::runtime_error("DEFERRED_RECOVERY_STOP_FAILED");
        }
        {
            auto adjusted = std::make_shared<workflow::FlowProgram>(*capture_definition.units.front().program);
            auto &root = adjusted->definitions.at("root");
            root.steps.at("checkpoint").guard = recognition::Request{
                "capture-check", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            capture_definition.units.front().program = std::move(adjusted);
            capture_definition.request_id = "native-capture-stop";
            runtime::NativeRunCoordinator capture(data_root / "capture-runs");
            auto blocked = std::make_shared<BlockingBackend>();
            capture.start(std::move(capture_definition), blocked);
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (!blocked->entered && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(5ms);
            if (!blocked->entered) throw std::runtime_error("CAPTURE_NOT_ENTERED");
            const auto stop_started = std::chrono::steady_clock::now();
            capture.request_stop();
            if (!capture.wait_for(500ms) ||
                std::chrono::steady_clock::now() - stop_started > 500ms ||
                capture.snapshot().state != contracts::RunState::UserStopped ||
                capture.snapshot().inputs.backend_called != 0)
                throw std::runtime_error("CAPTURE_STOP_FAILED");
        }
        std::cout << "native terminal storage, cleanup pending, deferred and capture stop passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
