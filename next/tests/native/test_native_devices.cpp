#include "devices/android_probe.hpp"
#include "devices/android_context_query.hpp"
#include "devices/android_viewport.hpp"
#include "devices/scrcpy_codec.hpp"
#include "devices/lifecycle_execution.hpp"
#include "devices/metadata_read_fault.hpp"
#include "devices/adb_failure.hpp"
#include "devices/native_input_gate.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include "platform/windows/mumu_binding.hpp"
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
class RecoveryPort final : public wvd::devices::LifecyclePort {
  public:
    wvd::devices::LifecycleTarget target{"device", "2", "game", "", false};
    bool exited{}, running{}, connected{}, game{};
    std::uint64_t generation{1};
    int called{};
    int transient_failures{};
    int start_without_focus{};
    bool timeout_after_effect{}, permanent_failure{}, exit_after_start{};
    std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
        return wvd::devices::LifecycleObservation{target, running, connected, game, true,
            generation, std::chrono::steady_clock::now(), game, exited};
    }
    bool execute_lifecycle(wvd::devices::LifecycleOperation op,
        const wvd::devices::LifecycleTarget &, const std::function<bool()> &) override {
        ++called;
        if (permanent_failure || transient_failures-- > 0) {
            if (timeout_after_effect) game = true;
            wvd::devices::AdbFailureInfo info;
            info.code = permanent_failure ? "ADB_UNAUTHORIZED" : "ADB_TIMEOUT";
            info.timed_out = !permanent_failure;
            info.retryable_transport = !permanent_failure;
            throw wvd::devices::AdbCommandFailure(info);
        }
        if (op == wvd::devices::LifecycleOperation::RestartInstance) {
            exited = false; running = connected = true; ++generation;
        } else if (op == wvd::devices::LifecycleOperation::StartApplication) {
            if (exit_after_start) { exited = true; running = connected = game = false; return true; }
            if (start_without_focus > 0) --start_without_focus;
            else game = true;
        }
        return true;
    }
};
template <typename Call>
void rejected(Call call, const char *reason) {
    try { call(); } catch (const std::exception &) { return; }
    throw std::runtime_error(reason);
}
}

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--input-context") {
            namespace a = wvd::devices::android;
            std::ifstream source(argv[2], std::ios::binary);
            if (!source) throw std::runtime_error("INPUT_CONTEXT_FIXTURE_MISSING");
            const std::string real{std::istreambuf_iterator<char>{source}, {}};
            const auto viewport = a::input_viewport(real);
            if (a::focus(real) != "jp.co.drecom.wizardry.daphne" || !viewport ||
                viewport->size != wvd::contracts::Size{900,1600} || viewport->rotation != 0)
                throw std::runtime_error("REAL_INPUT_CONTEXT_INVALID");
            unsigned checked{1};
            const auto check = [&](const std::string &text, const std::string &expected) {
                ++checked;
                if (a::focus(text) != expected) throw std::runtime_error("INPUT_FOCUS_CONTRACT_FAILED");
            };
            const std::string prefix = "Input Dispatcher State:\n  FocusedDisplayId: 0\n";
            const std::string game = "    displayId=0, name='123 game.pkg/MainActivity'\n";
            check(prefix + "  FocusedWindows:\n" + game, "game.pkg");
            check(prefix + "  FocusedApplications:\n" + game +
                "  FocusedWindows:\n    displayId=0, name='234 com.android.vpndialogs/ConfirmDialog'\n",
                "com.android.vpndialogs");
            check(prefix + "  FocusedWindows: <none>\n  FocusRequests:\n" + game, "");
            check(prefix + "  FocusedApplications:\n" + game, "");
            check(prefix + "  FocusedWindows:\n    displayId=0, name='StatusBar'\n", "");
            check(prefix + "  FocusedWindows:\n" + game + game, "");
            check(prefix + "  FocusedWindows:\n" + game +
                "    displayId=0, name='234 other.pkg/Activity'\n", "");
            check("Input Dispatcher State:\n  FocusedDisplayId: 1\n  FocusedWindows:\n" + game, "");
            check("Input Dispatcher State:\n  FocusedWindows:\n" + game, "");
            check(prefix + "  FocusedWindows:\n    displayId=1, name='123 game.pkg/Activity'\n", "");
            check(prefix + "  FocusedWindows:\n" + game +
                "  FocusRequests:\n    displayId=0, name='234 other.pkg/Activity' result='OK'\n", "game.pkg");
            check("Input Dispatcher State:\r\n  FocusedDisplayId:   0\r\n  FocusedWindows:\r\n"
                "    displayId = 0, name = '123 game.pkg/.Activity'\r\n", "game.pkg");
            check(prefix + "  FocusedDisplayId: 0\n  FocusedWindows:\n" + game, "");
            check("mCurrentFocus=Window{123 u0 game.pkg/Activity}\n", "");
            const auto combine = [](const std::string &before, const std::string &middle,
                                    const std::string &after) {
                return "__WVD_CTX_V1_BEGIN__\n" + before + "\n__WVD_CTX_V1_INPUT__\n" + middle +
                    "\n__WVD_CTX_V1_END_FOCUS__\n" + after + "\n__WVD_CTX_V1_STATUS__:0:0:0\n";
            };
            const auto joined = combine(real, real, real);
            const auto parsed = a::parse_context_dump(joined);
            if (!parsed || a::focus(std::string(parsed->before_focus)) != a::focus(real) ||
                a::focus(std::string(parsed->after_focus)) != a::focus(real) ||
                a::input_viewport(std::string(parsed->input)) != viewport)
                throw std::runtime_error("INPUT_CONTEXT_SECTIONS_INVALID");
            const auto changed = combine(real, real, prefix + "  FocusedWindows:\n"
                "    displayId=0, name='234 other.pkg/Activity'\n");
            const auto transition = a::parse_context_dump(changed);
            if (!transition || a::focus(std::string(transition->before_focus)) ==
                a::focus(std::string(transition->after_focus)))
                throw std::runtime_error("INPUT_FOCUS_CHANGE_LOST");
            if (a::parse_context_dump(joined + "__WVD_CTX_V1_INPUT__\n") ||
                a::parse_context_dump(joined.substr(0, joined.size()-3)) ||
                a::parse_context_dump(combine(real, real, "")))
                throw std::runtime_error("INCOMPLETE_INPUT_CONTEXT_ACCEPTED");
            const auto command = std::string(a::context_probe_command);
            if (command.find("dumpsys window") != std::string::npos ||
                command.find("dumpsys input") == std::string::npos)
                throw std::runtime_error("FULL_WINDOW_DUMP_REINTRODUCED");
            std::cout << "input context real fixture and " << checked << " focus checks passed\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--physical-gate-authority") {
            using namespace wvd;
            struct Backend final : devices::DeviceBackend {
                unsigned calls{};
                bool context_valid{true}, channel_ready{true}, expired{}, foreground_lost{};
                int rotation{};
                bool offline() const override { return true; }
                bool connect() override { return true; }
                bool input_channel_ready() const override { return channel_ready; }
                bool context_matches(const contracts::FrameIdentity &, const std::string &) override {
                    return context_valid;
                }
                devices::RawFrame capture() override {
                    devices::RawFrame frame;
                    frame.device_id = "device";
                    frame.viewport_id = "viewport";
                    frame.foreground_application = foreground_lost ? "other-app" : "game";
                    frame.instance_id = "2";
                    frame.instance_created_identity = "instance-created-1";
                    frame.size = {10, 10};
                    frame.connection_generation = 1;
                    frame.display_rotation = rotation;
                    frame.captured_at = std::chrono::steady_clock::now() -
                        (expired ? std::chrono::seconds(1) : std::chrono::seconds(0));
                    frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(300, 0);
                    return frame;
                }
                bool execute(const contracts::Command &) override { ++calls; return true; }
            };
            unsigned checked{}, submitted{}, rejected{};
            // AutoOff 这类输入允许只读策略参与 scene；只有独立按钮 target 可以授权。
            // 固定点、BACK 和 Swipe 也必须遵守同一观察合同，不以命令种类绕过门禁。
            for (const auto kind : {contracts::ActionKind::Click, contracts::ActionKind::ClickKey,
                                   contracts::ActionKind::Swipe}) {
                for (const std::string mode : {
                    "scene-readonly", "eligible", "target-readonly", "scene-nohit", "scene-error",
                    "target-nohit", "target-error", "stale-frame", "scene-other-frame",
                    "target-other-frame", "scene-other-device", "target-other-device",
                    "scene-other-instance", "target-other-creation", "scene-other-connection",
                    "wrong-epoch", "wrong-generation", "wrong-pack", "wrong-viewport",
                    "foreground-lost", "rotation-unknown", "expired-frame", "invalid-area",
                    "scene-forbidden", "capability-denied", "permission-denied", "context-changed",
                    "channel-changed", "stopped", "consumed-frame"}) {
                    Backend backend;
                    backend.foreground_lost = mode == "foreground-lost";
                    backend.rotation = mode == "rotation-unknown" ? -1 : 0;
                    backend.expired = mode == "expired-frame";
                    contracts::InputPolicy policy;
                    policy.device_id = "device";
                    policy.game_id = policy.application_id = "game";
                    policy.pack_revision = "pack";
                    policy.recognition_size = {10, 10};
                    policy.allowed_scenes = {"game"};
                    policy.permissions = policy.capabilities = {kind};
                    if (mode == "scene-forbidden") policy.allowed_scenes.clear();
                    if (mode == "capability-denied") policy.capabilities.clear();
                    if (mode == "permission-denied") policy.permissions.clear();
                    if (backend.expired) policy.max_frame_age = std::chrono::milliseconds(10);
                    devices::NativeInputGate gate(backend, policy, 1);
                    contracts::Command command;
                    command.kind = kind;
                    command.x = command.y = 5;
                    command.x2 = command.y2 = 8;
                    command.duration = 100;
                    command.key = 4;
                    contracts::Box area{0, 0, 10, 10};
                    const auto frame = gate.capture();
                    contracts::Observation scene, target;
                    scene.basis = target.basis = frame.identity;
                    scene.outcome = target.outcome = contracts::RecognitionOutcome::Hit;
                    scene.action_eligible = mode == "eligible";
                    target.action_eligible = mode != "target-readonly";
                    if (mode == "scene-nohit") scene.outcome = contracts::RecognitionOutcome::NoHit;
                    if (mode == "scene-error") scene.outcome = contracts::RecognitionOutcome::Error;
                    if (mode == "target-nohit") target.outcome = contracts::RecognitionOutcome::NoHit;
                    if (mode == "target-error") target.outcome = contracts::RecognitionOutcome::Error;
                    if (mode == "stale-frame") (void)gate.capture();
                    if (mode == "scene-other-frame") ++scene.basis.frame_id;
                    if (mode == "target-other-frame") ++target.basis.frame_id;
                    if (mode == "scene-other-device") scene.basis.device_id = "other-device";
                    if (mode == "target-other-device") target.basis.device_id = "other-device";
                    if (mode == "scene-other-instance") scene.basis.instance_id = "3";
                    if (mode == "target-other-creation") target.basis.instance_created_identity = "later-instance";
                    if (mode == "scene-other-connection") ++scene.basis.connection_generation;
                    if (mode == "wrong-epoch") ++scene.basis.action_epoch;
                    if (mode == "wrong-generation") ++scene.basis.generation;
                    if (mode == "wrong-pack") scene.basis.pack_revision = "other-pack";
                    if (mode == "wrong-viewport") scene.basis.viewport_id = "other-viewport";
                    // 相互一致也不够：两份旧观察仍须匹配门禁持有的本次帧。
                    if (mode == "wrong-epoch" || mode == "wrong-generation" ||
                        mode == "wrong-pack" || mode == "wrong-viewport") target.basis = scene.basis;
                    if (mode == "invalid-area") area.width = 11;
                    backend.context_valid = mode != "context-changed";
                    backend.channel_ready = mode != "channel-changed";
                    if (mode == "stopped") gate.stop();
                    const bool allowed = mode == "scene-readonly" || mode == "eligible" ||
                        mode == "consumed-frame";
                    const auto result = gate.submit(command, scene, target, area);
                    if (result.disposition != (allowed ? devices::InputDisposition::Submitted :
                                                       devices::InputDisposition::Rejected) ||
                        backend.calls != (allowed ? 1u : 0u))
                        throw std::runtime_error("PHYSICAL_GATE_CONTRACT:" + mode);
                    ++checked;
                    if (allowed) ++submitted;
                    else ++rejected;
                    if (mode == "consumed-frame") {
                        const auto replay = gate.submit(command, scene, target, area);
                        if (replay.disposition != devices::InputDisposition::Rejected || backend.calls != 1)
                            throw std::runtime_error("PHYSICAL_GATE_CONSUMED_FRAME_REUSED");
                        ++checked;
                        ++rejected;
                    }
                    if (gate.counts().backend_called != backend.calls)
                        throw std::runtime_error("PHYSICAL_GATE_BACKEND_COUNT_MISMATCH");
                }
                if (kind == contracts::ActionKind::ClickKey) continue;
                // 合法同帧目标不能授权区域外的固定点或 Swipe 终点。
                Backend backend;
                contracts::InputPolicy policy;
                policy.device_id = "device";
                policy.game_id = policy.application_id = "game";
                policy.pack_revision = "pack";
                policy.recognition_size = {10, 10};
                policy.allowed_scenes = {"game"};
                policy.permissions = policy.capabilities = {kind};
                devices::NativeInputGate gate(backend, policy, 1);
                const auto frame = gate.capture();
                contracts::Observation scene, target;
                scene.basis = target.basis = frame.identity;
                scene.outcome = target.outcome = contracts::RecognitionOutcome::Hit;
                scene.action_eligible = false;
                contracts::Command command;
                command.kind = kind;
                command.x = kind == contracts::ActionKind::Click ? 10 : 5;
                command.y = 5;
                command.x2 = 10;
                command.y2 = 5;
                command.duration = 100;
                const auto result = gate.submit(command, scene, target, {0, 0, 10, 10});
                if (result.disposition != devices::InputDisposition::Rejected || backend.calls ||
                    result.detail != "INPUT_OUTSIDE_ALLOWED_AREA")
                    throw std::runtime_error("PHYSICAL_GATE_OUTSIDE_AREA_INPUT");
                ++checked;
                ++rejected;
            }
            std::cout << "PASS real NativeInputGate: " << checked << " checks, " << submitted
                      << " allowed inputs, " << rejected << " rejections; readonly scene with an independent "
                         "eligible target is allowed, invalid evidence never reaches the backend\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--lifecycle-deadline") {
            using namespace std::chrono_literals;
            using Clock = std::chrono::steady_clock;
            struct SlowPort final : wvd::devices::LifecyclePort {
                bool stop{}, cancel_on_observe{};
                unsigned calls{};
                Clock::time_point window{};
                wvd::devices::LifecycleTarget target{"device","2","game","",false};
                void lifecycle_window(Clock::time_point d) noexcept override { window = d; }
                std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
                    std::this_thread::sleep_for(20ms); if (cancel_on_observe) stop = true;
                    return wvd::devices::LifecycleObservation{target,true,true,true,true,1,Clock::now(),true,false};
                }
                bool execute_lifecycle(wvd::devices::LifecycleOperation,const wvd::devices::LifecycleTarget &,
                    const std::function<bool()> &) override { ++calls; return true; }
            };
            for (const auto mode : {"step", "total", "cancel"}) {
                SlowPort port; port.cancel_on_observe = std::string(mode) == "cancel";
                wvd::devices::LifecyclePlan plan{port.target,{wvd::devices::LifecycleOperation::StartApplication},1};
                plan.step_timeout = std::string(mode) == "step" ? 1ms : 1s;
                const auto total = Clock::now() + (std::string(mode) == "total" ? 1ms : 1s);
                const auto end = wvd::devices::execute_lifecycle_plan(plan,port,[&]{return port.stop;},[](const auto &,const auto &){},total);
                if (end == wvd::devices::LifecycleEnd::ReadyForBoot || port.calls || port.window != Clock::time_point{} ||
                    (port.stop && end != wvd::devices::LifecycleEnd::Cancelled))
                    throw std::runtime_error("LATE_OBSERVATION_AUTHORIZED_LIFECYCLE");
                std::cout << "lifecycle-deadline " << mode << " passed\n";
            }
            return 0;
        }
        namespace d = wvd::devices;
        if (argc == 2 && std::string(argv[1]) == "--closure-metadata") {
            using J = nlohmann::json;
            const J timeout{{"success", false}, {"primary_error", "METADATA_TIMEOUT"},
                {"error", "METADATA_TIMEOUT"}, {"quiescent", true}, {"handles_released", true},
                {"helper_exited", true}, {"pending_io", 0}, {"elapsed_ms", 23.75}};
            bool retryable{};
            try { d::require_metadata_read(timeout, std::chrono::milliseconds{17}); }
            catch (const wvd::contracts::ObservationUnavailable &e) {
                retryable = e.fault().kind == wvd::contracts::ReadFaultKind::Timeout &&
                    e.fault().timeout.count() == 17 && e.fault().elapsed.count() == 23 &&
                    e.fault().details.at("elapsed_ms") == 23.75 &&
                    J::parse(e.fault().details.at("report").get<std::string>()) == timeout;
            }
            if (!retryable) throw std::runtime_error("META-01");
            const auto blocked = [&](J report) {
                try { d::require_metadata_read(report, std::chrono::milliseconds{17}); }
                catch (const wvd::contracts::ObservationUnavailable &) { throw std::runtime_error("META-02_RETRY_FORBIDDEN"); }
                catch (const std::runtime_error &e) {
                    if (std::string(e.what()).find(report.value("error", "")) == std::string::npos)
                        throw std::runtime_error("META-02_ORIGINAL_ERROR_LOST");
                    return;
                }
                throw std::runtime_error("META-02_NOT_BLOCKED");
            };
            for (const auto *field : {"quiescent", "handles_released", "helper_exited", "pending_io"}) {
                auto report = timeout; report.erase(field); blocked(report);
            }
            for (const auto *error : {"METADATA_CLEANUP_PENDING", "MUMU_INSTANCE_MISMATCH", "MUMU_ADB_BINDING_MISMATCH",
                "METADATA_JSON_INVALID", "METADATA_COMMAND_INVALID", "METADATA_CANCELLED", "OTHER_ERROR"}) {
                auto report = timeout; report["error"] = error;
                if (std::string(error) != "METADATA_CLEANUP_PENDING") report["primary_error"] = error;
                blocked(report);
            }
            std::cout << "META-01 META-02 PASS: classified report only; no helper or device launched\n";
            return 0;
        }
        // 只保护本次修复的准入边界；不把假端口结果当成真实MuMu恢复验收。
        const nlohmann::json crashed{{"error_code", 900}, {"is_process_started", false},
            {"is_android_started", false}};
        if (!wvd::platform::mumu_metadata_usable(crashed) ||
            !wvd::platform::mumu_metadata_usable({{"error_code",900}, {"is_process_started",false}, {"is_android_started",true}}) ||
            wvd::platform::mumu_metadata_usable({{"error_code",900}, {"is_process_started",true}, {"is_android_started",true}}) ||
            wvd::platform::mumu_metadata_usable({{"error_code", 900}}) ||
            !wvd::platform::mumu_metadata_usable({{"error_code", 900}, {"is_process_started", true},
                {"is_android_started", false}}) ||
            wvd::platform::mumu_metadata_usable({{"error_code", 901}, {"is_process_started", true}}))
            throw std::runtime_error("CRASH_METADATA_BOUNDARY_INVALID");
        RecoveryPort recovery;
        d::LifecyclePlan plan{recovery.target,
            {d::LifecycleOperation::RestartInstance, d::LifecycleOperation::StartApplication}, 1};
        const auto event = [](const auto &, const auto &) {};
        if (d::execute_lifecycle_plan(plan, recovery, [] { return false; }, event) !=
            d::LifecycleEnd::RetryRequired || recovery.called)
            throw std::runtime_error("UNPROVEN_INSTANCE_EXIT_RESTARTED");
        recovery.exited = true;
        if (d::execute_lifecycle_plan(plan, recovery, [] { return true; }, event) !=
            d::LifecycleEnd::Cancelled || recovery.called)
            throw std::runtime_error("CANCELLED_RECOVERY_STARTED");
        if (d::execute_lifecycle_plan(plan, recovery, [] { return false; }, event) !=
            d::LifecycleEnd::ReadyForBoot || recovery.called != 2 || !recovery.game)
            throw std::runtime_error("CONFIRMED_CRASH_RECOVERY_FAILED");
        for (const bool already_applied : {false, true}) {
            RecoveryPort slow;
            slow.running = slow.connected = true;
            slow.transient_failures = 1;
            slow.timeout_after_effect = already_applied;
            const d::LifecyclePlan start{slow.target, {d::LifecycleOperation::StartApplication}, 1};
            if (d::execute_lifecycle_plan(start, slow, [] { return false; }, event) !=
                    d::LifecycleEnd::ReadyForBoot || slow.called != (already_applied ? 1 : 2))
                throw std::runtime_error("LIFECYCLE_TIMEOUT_NOT_REOBSERVED");
            slow.game = false;
            slow.permanent_failure = true;
            rejected([&] { d::execute_lifecycle_plan(start, slow, [] { return false; }, event); },
                "PERMANENT_LIFECYCLE_FAILURE_RETRIED");
        }
        const auto owners = wvd::platform::sample_memory_owners();
        {
            RecoveryPort changed;
            changed.running = changed.connected = true; changed.exit_after_start = true;
            d::LifecyclePlan start{changed.target, {d::LifecycleOperation::StartApplication}, 1};
            start.step_timeout = std::chrono::milliseconds(200);
            std::string reason;
            const auto events = [&](const std::string &name, const nlohmann::json &data) {
                if (name == "lifecycle.retry_required") reason = data.value("reason", "");
            };
            if (d::execute_lifecycle_plan(start, changed, [] { return false; }, events) !=
                d::LifecycleEnd::RetryRequired || reason != "DEVICE_STATE_CHANGED" || changed.called != 1)
                throw std::runtime_error("LIFECYCLE_EXIT_WAITED_FOR_OLD_POSTCONDITION");
        }
        {
            RecoveryPort delayed;
            delayed.running = delayed.connected = true;
            delayed.start_without_focus = 1;
            d::LifecyclePlan start{delayed.target, {d::LifecycleOperation::StartApplication}, 1};
            start.step_timeout = std::chrono::seconds(5);
            unsigned retries = 0;
            const auto events = [&](const std::string &name, const nlohmann::json &) {
                if (name == "lifecycle.backend_retried") ++retries;
            };
            if (d::execute_lifecycle_plan(start, delayed, [] { return false; }, events) !=
                    d::LifecycleEnd::ReadyForBoot || delayed.called != 2 || retries != 1)
                throw std::runtime_error("START_ACK_WITHOUT_FOCUS_NOT_RETRIED");
        }
        if (!owners.available || !owners.count || owners.count > 8 || owners.examined > 4096)
            throw std::runtime_error("MEMORY_OWNERS_SNAPSHOT_INVALID");
        for (unsigned i = 1; i < owners.count; ++i)
            if (owners.top[i - 1].private_bytes < owners.top[i].private_bytes)
                throw std::runtime_error("MEMORY_OWNERS_NOT_SORTED");
        wvd::contracts::Command click;
        click.kind = wvd::contracts::ActionKind::Click;
        click.x = 899;
        click.y = 1599;
        const auto messages = d::scrcpy::encode(click, 900, 1600);
        if (messages.size() != 2 || messages[0].size() != 32 ||
            messages[0][0] != 2 || messages[0][1] != 0 ||
            messages[0][12] != 3 || messages[0][13] != 131 ||
            messages[0][16] != 6 || messages[0][17] != 63 ||
            messages[0][18] != 3 || messages[0][19] != 132 ||
            messages[0][20] != 6 || messages[0][21] != 64 ||
            messages[0][22] != 255 || messages[0][23] != 255 ||
            messages[1][1] != 1 || messages[1][22] != 0 || messages[1][23] != 0)
            throw std::runtime_error("SCRCPY_CLICK_WIRE_INVALID");
        click.click_pair_interval_ms = 100;
        const auto pair = d::scrcpy::encode(click, 900, 1600);
        if (pair.size() != 4 || pair[0] != messages[0] || pair[1] != messages[1] ||
            pair[2] != messages[0] || pair[3] != messages[1])
            throw std::runtime_error("SCRCPY_CLICK_PAIR_WIRE_INVALID");
        click.click_pair_interval_ms = 500;
        rejected([&] { (void)d::scrcpy::encode(click, 900, 1600); }, "SCRCPY_OLD_500MS_PAIR_ACCEPTED");
        click.click_pair_interval_ms = 1;
        rejected([&] { (void)d::scrcpy::encode(click, 900, 1600); }, "SCRCPY_UNBOUNDED_PAIR_ACCEPTED");
        click.click_pair_interval_ms = 0;
        click.x = 900;
        rejected([&] { (void)d::scrcpy::encode(click, 900, 1600); },
            "SCRCPY_INVALID_COORDINATE_ACCEPTED");
        wvd::contracts::Command swipe;
        swipe.kind = wvd::contracts::ActionKind::Swipe;
        swipe.x = 100; swipe.y = 200; swipe.x2 = 500; swipe.y2 = 600;
        swipe.duration = 100;
        const auto motion = d::scrcpy::encode(swipe, 900, 1600);
        if (motion.size() != 5 || motion.front()[1] != 0 ||
            motion[1][1] != 2 || motion.back()[1] != 1)
            throw std::runtime_error("SCRCPY_SWIPE_WIRE_INVALID");
        wvd::contracts::Command back;
        back.kind = wvd::contracts::ActionKind::ClickKey;
        back.key = 4;
        const auto keys = d::scrcpy::encode(back, 900, 1600);
        if (keys.size() != 2 || keys[0].size() != 14 || keys[0][5] != 4 ||
            keys[0][1] != 0 || keys[1][1] != 1)
            throw std::runtime_error("SCRCPY_KEY_WIRE_INVALID");
        const auto reply = d::android::parse_shell_reply("123\nWVD_RC_0", "WVD_RC_");
        if (reply.exit_code != 0 || reply.output != "123" ||
            d::android::process(reply) != d::android::Presence::Present ||
            d::android::process({1, ""}) != d::android::Presence::Absent)
            throw std::runtime_error("ADB_REPLY_SEMANTICS_INVALID");
        rejected([&] { (void)d::android::parse_shell_reply("123", "WVD_RC_"); },
            "ADB_MISSING_TRAILER_ACCEPTED");
        rejected([&] { (void)d::android::process({0, ""}); },
            "ADB_EMPTY_SUCCESS_ACCEPTED");
        std::cout << "scrcpy wire and ADB result boundaries passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
