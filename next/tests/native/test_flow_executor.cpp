#include "runtime/flow_executor.hpp"
#include "devices/metadata_read_fault.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace {
using namespace wvd;
using namespace std::chrono_literals;
struct Ports : runtime::FlowPorts {
    int captures{}, operations{};
    bool fail_operation{};
    std::uint64_t first_operation_frame{}, second_operation_frame{};
    contracts::FrameEnvelope capture() override {
        contracts::FrameEnvelope frame;
        frame.identity.device_id = "offline";
        frame.identity.game_id = "wvd";
        frame.identity.pack_revision = "frozen";
        frame.identity.generation = 1;
        frame.identity.connection_generation = 1;
        frame.identity.frame_id = ++captures;
        frame.identity.recognition_size = {900, 1600};
        frame.identity.captured_at = std::chrono::steady_clock::now();
        return frame;
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &) override {
        contracts::Observation hit;
        hit.basis = frame.identity;
        hit.outcome = contracts::RecognitionOutcome::Hit;
        return hit;
    }
    runtime::Submission submit(const contracts::Command &,
        const contracts::Observation &, const contracts::Observation &,
        contracts::Box, const std::string &) override {
        throw std::runtime_error("UNEXPECTED_INPUT");
    }
    runtime::OperationResult operate(const std::string &,
        const nlohmann::json &,
        const std::optional<contracts::FrameEnvelope> &frame,
        const std::optional<contracts::Observation> &,
        const std::string &) override {
        if (fail_operation) return {runtime::OperationState::Failed, "OPERATION_FAILED"};
        if (!frame) throw std::runtime_error("MISSING_OPERATION_FRAME");
        if (++operations == 1) {
            first_operation_frame = frame->identity.frame_id;
            return {runtime::OperationState::Waiting, {},
                    std::chrono::steady_clock::now()};
        }
        second_operation_frame = frame->identity.frame_id;
        return {runtime::OperationState::Done};
    }
    bool cancelled() const override { return false; }
};

struct ExitPorts final : Ports {
    int event_probes{};
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &request) override {
        auto observation = Ports::recognize(frame, request);
        if (request.recognizer_id == "event.network" && ++event_probes > 3)
            observation.outcome = contracts::RecognitionOutcome::NoHit;
        return observation;
    }
};

struct LayerPorts final : Ports {
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &request) override {
        auto observation = Ports::recognize(frame, request);
        const auto id = frame.identity.frame_id;
        if ((request.recognizer_id == "candidate.finish" && id < 5) ||
            (request.recognizer_id == "event.network" && id != 2 && id != 3) ||
            (request.recognizer_id == "event.chest" && id >= 5))
            observation.outcome = contracts::RecognitionOutcome::NoHit;
        return observation;
    }
};
struct PendingEventPorts final : Ports {
    std::uint64_t epoch{};
    int network_probes{};
    std::string seen;
    contracts::FrameEnvelope capture() override {
        auto frame = Ports::capture();
        frame.identity.raw_size = {900, 1600};
        frame.identity.action_epoch = epoch;
        return frame;
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &request) override {
        auto result = Ports::recognize(frame, request);
        seen += request.recognizer_id + ":" + std::to_string(frame.identity.frame_id) + ",";
        // 按“输入在途、网络尚未处理”建场景，不依赖进入 Await 前浪费一张截图。
        if (request.recognizer_id == "event.network" && (!epoch || ++network_probes > 1))
            result.outcome = contracts::RecognitionOutcome::NoHit;
        result.box = contracts::Box{100, 100, 20, 20};
        result.center = contracts::Point{110, 110};
        return result;
    }
    runtime::Submission submit(const contracts::Command &,
        const contracts::Observation &, const contracts::Observation &,
        contracts::Box, const std::string &) override {
        ++epoch;
        return {runtime::SubmissionState::Accepted, epoch,
            std::chrono::steady_clock::now(), {}};
    }
};
struct MenuRetryPorts : Ports {
    std::string mode;
    std::uint64_t epoch{};
    int ready_checks{};
    contracts::FrameEnvelope capture() override {
        auto frame = Ports::capture();
        frame.identity.action_epoch = epoch;
        frame.identity.raw_size = {900, 1600};
        return frame;
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &request) override {
        auto hit = Ports::recognize(frame, request);
        hit.box = contracts::Box{100, 100, 20, 20};
        hit.center = contracts::Point{110, 110};
        hit.action_eligible = true;
        if (request.recognizer_id == "result" && (epoch < 2 || mode == "no_progress"))
            hit.outcome = contracts::RecognitionOutcome::NoHit;
        if (request.recognizer_id == "retry.ready" &&
            (mode == "unknown_page" || (mode == "changing_hint" && ++ready_checks <= 2)))
            hit.outcome = contracts::RecognitionOutcome::NoHit;
        return hit;
    }
    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
        const contracts::Observation &, contracts::Box, const std::string &) override {
        return {mode == "delivery_unknown" ? runtime::SubmissionState::Unresolved : runtime::SubmissionState::Accepted,
            ++epoch, std::chrono::steady_clock::now(), {}};
    }
};

// 核心验收对象是生产 FlowExecutor 的分派/回执行为；识图与设备输入在此隔离。
struct MismatchPorts final : Ports {
    std::uint64_t epoch{};
    int exception_probes{}, lower_probes{}, special_probes{}, handler_calls{};
    int result_probes{};
    bool interrupted{}, handled{}, unknown{}, animation{}, before_input{};
    contracts::FrameEnvelope capture() override {
        auto frame = Ports::capture();
        frame.identity.raw_size = {900, 1600};
        frame.identity.action_epoch = epoch;
        return frame;
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &request) override {
        auto value = Ports::recognize(frame, request);
        value.box = contracts::Box{100, 100, 20, 20};
        value.center = contracts::Point{110, 110};
        if (request.recognizer_id == "scene" && (epoch || before_input) && interrupted && !handled)
            value.outcome = contracts::RecognitionOutcome::NoHit;
        if (request.recognizer_id == "scene" && epoch && animation && ++result_probes <= 2)
            value.outcome = contracts::RecognitionOutcome::NoHit;
        if (request.recognizer_id == "never") value.outcome = contracts::RecognitionOutcome::NoHit;
        if (request.recognizer_id == "event.network") {
            ++exception_probes;
            if (!interrupted || handled || unknown) value.outcome = contracts::RecognitionOutcome::NoHit;
        } else if (request.recognizer_id == "event.lower") {
            ++lower_probes;
            value.outcome = contracts::RecognitionOutcome::NoHit;
        } else if (request.recognizer_id == "event.special") {
            ++special_probes;
            value.outcome = contracts::RecognitionOutcome::NoHit;
        }
        return value;
    }
    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
        const contracts::Observation &, contracts::Box, const std::string &) override {
        return {runtime::SubmissionState::Accepted, ++epoch, std::chrono::steady_clock::now(), {}};
    }
    runtime::OperationResult operate(const std::string &, const nlohmann::json &,
        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
        const std::string &) override {
        ++handler_calls;
        handled = true;
        return {runtime::OperationState::Done};
    }
};

workflow::Step step(std::string id, workflow::StepData data,
                    std::vector<std::string> next = {}) {
    workflow::Step result;
    result.id = std::move(id);
    result.source_path = result.id;
    result.data = std::move(data);
    result.next = std::move(next);
    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--timeout-reclassification") {
            struct ScenePorts final : Ports {
                bool moved{};
                int stale_checks{};
                contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                                  const recognition::Request &request) override {
                    auto result = Ports::recognize(frame, request);
                    if (request.recognizer_id == "stale" && ++stale_checks > 1)
                        result.outcome = contracts::RecognitionOutcome::NoHit;
                    if (request.recognizer_id == "battle" && !moved)
                        result.outcome = contracts::RecognitionOutcome::NoHit;
                    return result;
                }
            };
            for (bool moved : {false, true}) {
                recognition::Request stale{"stale", "1", {0, 0, 900, 1600},
                    recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                auto battle = stale; battle.recognizer_id = "battle";
                workflow::FlowProgram program; program.revision = "timeout-scene"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "entry";
                auto entry = step("entry", workflow::Route{}, {"stale"});
                root.steps.emplace("entry", std::move(entry));
                auto old = step("stale", workflow::Observe{stale}, {"done"});
                old.guard = stale; old.time_limit = 60ms;
                root.steps.emplace("stale", std::move(old));
                auto new_scene = step("battle", workflow::Observe{battle}, {"done"});
                new_scene.guard = battle; new_scene.marks_known_scene = true;
                root.steps.emplace("battle", std::move(new_scene));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                program.definitions.emplace("root", std::move(root));
                ScenePorts ports; ports.moved = moved;
                runtime::FlowExecutor executor(program, ports, 1s);
                runtime::TickResult result;
                for (int i = 0; i < 100; ++i) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Waiting && result.state != runtime::TickState::Progress) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                if (moved ? result.state != runtime::TickState::Completed :
                    (result.state != runtime::TickState::Failed || result.code != "FLOW_STAGE_TIMEOUT"))
                    throw std::runtime_error("TIMEOUT_RECLASSIFICATION:" + result.code);
                if (ports.operations || executor.has_unresolved_input())
                    throw std::runtime_error("TIMEOUT_RECLASSIFICATION_REPLAYED_INPUT");
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--instance-start-window") {
            struct RecoveryPorts final : Ports {
                int attempts{};
                std::string fault_code;
                contracts::FrameEnvelope capture() override {
                    throw contracts::ObservationUnavailable({contracts::ReadFaultKind::TransportUnavailable,
                        contracts::ReadFaultStage::Capture, "ADB_OFFLINE", "bound_device.capture", {}, {}});
                }
                contracts::ObservationRecovery recover_observation(bool restart_application = false) override {
                    ++attempts;
                    throw contracts::ObservationUnavailable({contracts::ReadFaultKind::TransportUnavailable,
                        contracts::ReadFaultStage::Capture, fault_code, "bound_device.reconnect", {}, {}});
                }
            } ports;
            recognition::Request scene{"scene", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            workflow::FlowProgram program; program.revision = "instance-window"; program.root_definition = "root";
            workflow::Definition root; root.id = "root"; root.entry = "scene";
            root.steps.emplace("scene", step("scene", workflow::Observe{scene}, {"done"}));
            root.steps.emplace("done", step("done", workflow::Finish{}));
            program.definitions.emplace("root", std::move(root));
            for (const char *code : {"DEVICE_INSTANCE_RESTART_REQUIRED", "DEVICE_INSTANCE_STARTING", "DEVICE_RECONNECT_WAIT"}) {
                ports.attempts = 0; ports.fault_code = code;
                runtime::FlowExecutor executor(program, ports, 4min, {60s, 1ms, 2ms});
                for (int i = 0; i < 10 && !ports.attempts; ++i) {
                    const auto result = executor.tick();
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                const auto recovery = executor.progress_snapshot().at("observation_recovery");
                const int expected = ports.fault_code == "DEVICE_RECONNECT_WAIT" ? 60000 : 180000;
                if (ports.attempts != 1 || recovery.at("outage_limit_ms") != expected ||
                    recovery.at("code") != code)
                    throw std::runtime_error("INSTANCE_START_WINDOW_NOT_EVIDENCE_BASED");
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--selection-race") {
            // 导航停止分类选中后，新帧进入战斗；执行器应重选，而非卡在旧 Observe。
            for (const char *mode : {"guarded", "unguarded", "flapping"}) {
                struct RacePorts final : Ports {
                    int stopped_checks{};
                    bool flapping{}, unguarded{};
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                                      const recognition::Request &request) override {
                        auto result = Ports::recognize(frame, request);
                        const bool hit = request.recognizer_id == "stopped"
                            ? (++stopped_checks % 2 == 1 && !unguarded)
                            : (!flapping && stopped_checks > 0);
                        if (!hit) result.outcome = contracts::RecognitionOutcome::NoHit;
                        return result;
                    }
                } ports;
                ports.flapping = std::string(mode) == "flapping";
                ports.unguarded = std::string(mode) == "unguarded";
                recognition::Request stopped{"stopped", "1", {0, 0, 900, 1600},
                    recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                auto battle = stopped; battle.recognizer_id = "battle";
                workflow::FlowProgram p; p.revision = "selection-race"; p.root_definition = "root";
                workflow::Definition d; d.id = "root"; d.entry = "dispatch";
                auto dispatch = step("dispatch", workflow::Route{}, {"battle", "stopped"});
                dispatch.time_limit = 180ms;
                d.steps.emplace("dispatch", std::move(dispatch));
                auto observe = step("stopped", workflow::Observe{stopped}, {"dispatch"});
                if (std::string(mode) != "unguarded") observe.guard = stopped;
                d.steps.emplace("stopped", std::move(observe));
                auto finish = step("battle", workflow::Finish{}); finish.guard = battle;
                d.steps.emplace("battle", std::move(finish));
                p.definitions.emplace("root", std::move(d));
                runtime::FlowExecutor executor(p, ports, 2s);
                runtime::TickResult result;
                const auto began = std::chrono::steady_clock::now();
                for (int i = 0; i < 100; ++i) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Waiting && result.state != runtime::TickState::Progress) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                if (ports.flapping ? (result.state != runtime::TickState::Failed ||
                    result.code != "FLOW_STAGE_TIMEOUT" || std::chrono::steady_clock::now() - began > 1s)
                    : result.state != runtime::TickState::Completed)
                    throw std::runtime_error(std::string("SELECTION_RACE:") + mode + ":" + result.code);
                if (ports.operations || executor.has_unresolved_input())
                    throw std::runtime_error("SELECTION_RACE_CHANGED_INPUT_STATE");
                std::cout << "selection " << mode << ": " << result.code << " no input/replay\n";
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--exception-restart") {
            // 只缩短注入的时钟门槛，执行同一生产重启/Boot/原调用恢复路径。
            // 设备执行仍须由本轮真实黑屏现场另行验收。
            struct ExceptionPorts final : Ports {
                bool restored{}, progressing{}, stop{};
                int restarts{}, boots{}, prepares{};
                contracts::ObservationRecovery recover_observation(bool restart_application = false) override {
                    if (!restart_application) throw std::runtime_error("EXCEPTION_RESTART_REQUEST_MISSING");
                    ++restarts; restored = true;
                    return {{}, true, true};
                }
                contracts::Observation recognize(const contracts::FrameEnvelope &frame, const recognition::Request &request) override {
                    auto result = Ports::recognize(frame, request);
                    if (request.recognizer_id == "ready" && !restored && !(progressing && captures > 20))
                        result.outcome = contracts::RecognitionOutcome::NoHit;
                    return result;
                }
                runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                    const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                    const std::string &) override {
                    if (binding == "Prepare") ++prepares;
                    if (binding == "Boot") ++boots;
                    return {runtime::OperationState::Done};
                }
                bool cancelled() const override { return stop; }
            };
            for (const auto *mode : {"black", "progress", "stop"}) {
                workflow::FlowProgram program; program.revision = "exception-restart"; program.root_definition = "root";
                recognition::Request ready{"ready", "1", {0,0,900,1600}, recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                workflow::Definition root; root.id = "root"; root.entry = "prepare";
                root.steps.emplace("prepare", step("prepare", workflow::RegisteredOperation{"Prepare", nlohmann::json::object()}, {"work"}));
                root.steps.emplace("work", step("work", workflow::Call{"work", {}}, {"done"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition work; work.id = "work"; work.entry = "dispatch";
                work.steps.emplace("dispatch", step("dispatch", workflow::Route{}, {"ready", "poll"}));
                auto found = step("ready", workflow::Observe{ready}, {"return"}); found.guard = ready;
                work.steps.emplace("ready", std::move(found));
                work.steps.emplace("poll", step("poll", workflow::Poll{2ms, {}, {}}, {"dispatch"}));
                work.steps.at("dispatch").max_hit = work.steps.at("poll").max_hit = 0;
                // 局部10ms到期仍要交异常窗口，而不是停止；正常进行的显式Poll则不重启。
                work.steps.at("dispatch").time_limit = 10ms;
                work.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::Definition boot; boot.id = "boot"; boot.entry = "boot";
                boot.steps.emplace("boot", step("boot", workflow::RegisteredOperation{"Boot", nlohmann::json::object()}, {"return"}));
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::EventRule event; event.id = "context-restart"; event.detect = ready;
                event.handler_definition = "boot"; event.on_device_restart = true; root.events.push_back(event);
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("work", std::move(work));
                program.definitions.emplace("boot", std::move(boot));
                ExceptionPorts ports; ports.progressing = std::string(mode) == "progress";
                if (ports.progressing) {
                    auto ongoing = ready; ongoing.recognizer_id = "moving";
                    std::get<workflow::Poll>(program.definitions.at("work").steps.at("poll").data).ongoing = ongoing;
                    program.definitions.at("work").steps.at("dispatch").time_limit = 1s;
                }
                runtime::FlowExecutor executor(program, ports, 2s, {100ms, 2ms, 5ms, 30ms});
                runtime::TickResult result;
                for (int i = 0; i < 500; ++i) {
                    result = executor.tick();
                    if (std::string(mode) == "stop" && ports.captures > 3) ports.stop = true;
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                const bool black = std::string(mode) == "black";
                if (ports.prepares != 1 || ports.restarts != (black ? 1 : 0) || ports.boots != (black ? 1 : 0) ||
                    result.state != (std::string(mode) == "stop" ? runtime::TickState::Cancelled : runtime::TickState::Completed))
                    throw std::runtime_error(std::string("EXCEPTION_RESTART:") + mode + ":" + result.code +
                        ":restarts=" + std::to_string(ports.restarts) + ":boots=" + std::to_string(ports.boots));
                std::cout << "exception " << mode << " restarts=" << ports.restarts << " prepares=" << ports.prepares << " result=" << result.code << '\n';
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--closure-recovery") {
            struct ReadPorts final : MenuRetryPorts {
                int recoveries{};
                int prepares{}, confirms{};
                bool transfer_pending{};
                bool recovered{}, forever{}, stop{};
                std::string recovery_mode;
                std::chrono::steady_clock::time_point window{};
                void observation_window(std::chrono::steady_clock::time_point value) override { window = value; }
                contracts::FrameEnvelope capture() override {
                    if (epoch && !recovered)
                        throw contracts::ObservationUnavailable({recovery_mode.empty() ? contracts::ReadFaultKind::TransportUnavailable
                            : contracts::ReadFaultKind::ApplicationUnavailable,
                            contracts::ReadFaultStage::Capture, recovery_mode.empty() ? "ADB_OFFLINE" : "GAME_NOT_FOREGROUND",
                            "offline.capture", {}, {}});
                    return MenuRetryPorts::capture();
                }
                contracts::ObservationRecovery recover_observation(bool restart_application = false) override {
                    if (++recoveries <= 2 || forever) {
                        devices::require_metadata_read({{"success", false}, {"error", "METADATA_TIMEOUT"},
                            {"primary_error", "METADATA_TIMEOUT"}, {"quiescent", true}, {"handles_released", true},
                            {"helper_exited", true}, {"pending_io", 0}, {"elapsed_ms", 1}},
                            std::chrono::duration_cast<std::chrono::milliseconds>(window - std::chrono::steady_clock::now()));
                    }
                    recovered = true;
                    return {{}, recovery_mode.starts_with("restart") || recovery_mode == "unknown" || recovery_mode == "effect",
                        !recovery_mode.empty()};
                }
                bool cancelled() const override { return stop; }
                runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                    const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                    const std::string &) override {
                    if (binding == "Boot") ++operations;
                    if (binding == "Prepare") { ++prepares; transfer_pending = true; }
                    if (binding == "Confirm") {
                        if (!transfer_pending) throw std::runtime_error("RESTART_LOST_PREPARED_TRANSFER");
                        ++confirms; transfer_pending = false;
                    }
                    return {runtime::OperationState::Done};
                }
                contracts::Observation recognize(const contracts::FrameEnvelope &f, const recognition::Request &r) override {
                    auto result = Ports::recognize(f, r);
                    result.box = contracts::Box{100,100,20,20}; result.center = contracts::Point{110,110};
                    result.action_eligible = true;
                    if (recovery_mode == "restart-nested" &&
                        ((r.recognizer_id == "old-menu" && operations) ||
                         (r.recognizer_id == "new-menu" && !operations) ||
                         (r.recognizer_id == "menu-result" && epoch < 2)))
                        result.outcome = contracts::RecognitionOutcome::NoHit;
                    return result;
                }
            };
            for (const auto *scenario : {"recover", "exhaust", "stop", "focus", "restart", "restart-nested", "restart-gold", "unknown", "effect"}) {
                workflow::FlowProgram program; program.revision = "closure-read"; program.root_definition = "root";
                recognition::Request probe{"scene", "1", {0,0,900,1600}, recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                workflow::Definition root; root.id = "root"; root.entry = "input";
                root.steps.emplace("input", step("input", workflow::Input{probe, probe, {{"kind","Click"}}, {0,0,900,1600}}, {"await"}));
                root.steps.emplace("await", step("await", workflow::AwaitResult{probe, 1s, 0ms, 1ms}, {"done"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                const std::string mode = scenario;
                if (mode.starts_with("restart") || mode == "unknown" || mode == "effect") {
                    auto &input = std::get<workflow::Input>(root.steps.at("input").data);
                    input.retry = workflow::InputRetry{probe, 1s, 2};
                    if (mode == "effect") input.effect_binding = "Effect";
                    if (mode == "restart-gold") {
                        input.effect_binding = "Effect"; input.retry->restart_from = "dispatch";
                        root.steps.emplace("dispatch", step("dispatch", workflow::Route{}, {"input"}));
                        root.steps.at("input").max_hit = root.steps.at("await").max_hit = 2;
                        root.entry = "dispatch";
                    }
                    workflow::Definition boot; boot.id = "boot"; boot.entry = "boot";
                    boot.steps.emplace("boot", step("boot", workflow::RegisteredOperation{"Boot", nlohmann::json::object()}, {"return"}));
                    boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                    program.definitions.emplace("boot", std::move(boot));
                    workflow::EventRule event;
                    event.id = "context-restart"; event.detect = probe; event.handler_definition = "boot";
                    event.on_device_restart = true;
                    root.events.push_back(std::move(event));
                }
                if (mode == "restart-nested") {
                    // 复现父业务已prepare、子菜单被重启打断；恢复后菜单候选已改变。
                    // 必须在原Call内重新选路，不能重跑根Prepare或把旧点击记成成功。
                    root.steps.clear(); root.entry = "prepare";
                    root.steps.emplace("prepare", step("prepare", workflow::RegisteredOperation{"Prepare", nlohmann::json::object()}, {"call"}));
                    root.steps.emplace("call", step("call", workflow::Call{"navigation", {}}, {"confirm"}));
                    root.steps.emplace("confirm", step("confirm", workflow::RegisteredOperation{"Confirm", nlohmann::json::object()}, {"done"}));
                    root.steps.emplace("done", step("done", workflow::Finish{}));
                    workflow::Definition navigation; navigation.id = "navigation"; navigation.entry = "dispatch";
                    navigation.steps.emplace("dispatch", step("dispatch", workflow::Route{}, {"old", "new"}));
                    auto result_probe = probe; result_probe.recognizer_id = "menu-result";
                    for (const auto *name : {"old", "new"}) {
                        auto menu = probe; menu.recognizer_id = std::string(name) + "-menu";
                        workflow::Input input{menu, menu, {{"kind", "Click"}}, {0,0,900,1600}};
                        input.retry = workflow::InputRetry{menu, 1s, 2};
                        auto node = step(name, std::move(input), {std::string(name) + "-await"}); node.guard = menu;
                        navigation.steps.emplace(name, std::move(node));
                        navigation.steps.emplace(std::string(name) + "-await",
                            step(std::string(name) + "-await", workflow::AwaitResult{result_probe, 1s, 0ms, 1ms}, {"return"}));
                    }
                    navigation.steps.emplace("return", step("return", workflow::Return{"completed"}));
                    program.definitions.emplace("navigation", std::move(navigation));
                }
                program.definitions.emplace("root", std::move(root));
                ReadPorts ports; ports.forever = std::string(scenario) == "exhaust";
                if (mode == "focus" || mode.starts_with("restart") || mode == "unknown" || mode == "effect") ports.recovery_mode = mode;
                if (mode == "unknown") ports.mode = "delivery_unknown";
                runtime::FlowExecutor executor(program, ports, 2s, {80ms, 5ms, 10ms});
                nlohmann::json pending;
                runtime::TickResult result;
                const auto began = std::chrono::steady_clock::now();
                for (int i=0; i<300; ++i) {
                    result = executor.tick();
                    const auto state = executor.progress_snapshot();
                    const auto current = state.at("pending_inputs");
                    if (!current.empty()) {
                        if (pending.is_null()) pending = current;
                        else if (pending != current && !(mode.starts_with("restart") && ports.operations == 1 && ports.epoch == 2))
                            throw std::runtime_error("READ_PENDING_CHANGED");
                    }
                    if (std::string(scenario) == "stop" && ports.recoveries) ports.stop = true;
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                if (ports.epoch != (mode.starts_with("restart") ? 2 : 1) || pending.is_null())
                    throw std::runtime_error("READ_INPUT_COUNT_OR_BASIS_LOST:" + mode + ":" + result.code + ":" + std::to_string(ports.epoch));
                if (std::string(scenario) == "recover" && (result.state != runtime::TickState::Completed || ports.recoveries != 3))
                    throw std::runtime_error("READ-01:" + result.code);
                if (std::string(scenario) == "exhaust" && (result.state != runtime::TickState::Failed ||
                    std::chrono::steady_clock::now() - began > 1s || !executor.has_unresolved_input()))
                    throw std::runtime_error("READ-02:" + result.code);
                if (std::string(scenario) == "stop" && (result.state != runtime::TickState::Cancelled || !executor.has_unresolved_input()))
                    throw std::runtime_error("READ-03:" + result.code);
                if (mode == "focus" && (result.state != runtime::TickState::Completed || ports.operations))
                    throw std::runtime_error("FOREGROUND_RESTORE_RESTARTED_BUSINESS:" + result.code);
                if (mode.starts_with("restart") && (result.state != runtime::TickState::Completed || executor.has_unresolved_input() || ports.operations != 1))
                    throw std::runtime_error("MENU_RESTART_NOT_REPLANNED:" + result.code);
                if (mode == "restart-nested" && (ports.prepares != 1 || ports.confirms != 1 || ports.transfer_pending))
                    throw std::runtime_error("MENU_RESTART_REPLAYED_ROOT_BUSINESS");
                if ((mode == "unknown" || mode == "effect") && (result.state != runtime::TickState::Completed || ports.operations != 1))
                    throw std::runtime_error("PROTECTED_INPUT_NOT_REOBSERVED:" + result.code);
                std::cout << "READ " << scenario << " inputs=" << ports.epoch << " recoveries=" << ports.recoveries << " result=" << result.code << '\n';
            }
            return 0;
        }
        // 只隔离图像来源，实际执行生产 Poll/期限逻辑；不能以此替代实机完整一轮。
        struct ProgressPorts final : Ports {
            bool progressing{};
            contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                              const recognition::Request &request) override {
                auto result = Ports::recognize(frame, request);
                if ((request.recognizer_id == "done" && captures < 24) ||
                    (request.recognizer_id == "changed" && !progressing))
                    result.outcome = contracts::RecognitionOutcome::NoHit;
                return result;
            }
        };
        for (const bool changing : {true, false}) {
            recognition::Request ongoing{"ongoing", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            auto changed = ongoing, done = ongoing;
            changed.recognizer_id = "changed";
            done.recognizer_id = "done";
            workflow::FlowProgram p;
            p.revision = "progress-deadline";
            p.root_definition = "root";
            workflow::Definition d;
            d.id = "root"; d.entry = "wait";
            auto poll = step("wait", workflow::Poll{10ms, ongoing, changed}, {"done", "wait"});
            poll.max_hit = 0; poll.time_limit = 80ms;
            d.steps.emplace("wait", std::move(poll));
            auto finish = step("done", workflow::Finish{});
            finish.guard = done;
            d.steps.emplace("done", std::move(finish));
            p.definitions.emplace("root", std::move(d));
            ProgressPorts ports;
            ports.progressing = changing;
            runtime::FlowExecutor executor(p, ports, 2s);
            runtime::TickResult end;
            for (int i = 0; i < 200; ++i) {
                end = executor.tick();
                if (end.state == runtime::TickState::Waiting) std::this_thread::sleep_until(end.wake_at);
                if (end.state != runtime::TickState::Waiting && end.state != runtime::TickState::Progress) break;
            }
            if (changing ? end.state != runtime::TickState::Completed :
                (end.state != runtime::TickState::Failed || end.code != "FLOW_STAGE_TIMEOUT"))
                throw std::runtime_error("PROGRESS_WAIT_CONTRACT_FAILED:" + end.code);
        }
        // 五个新契约先单独验证；后面的既有检查仍执行且保留原断言/失败。
        for (const char *mode : {"retry", "changing_hint", "unknown_page", "non_repeatable", "delivery_unknown", "no_progress"}) {
            workflow::FlowProgram menu;
            menu.revision = std::string("menu-retry-") + mode;
            menu.root_definition = "root";
            recognition::Request scene{"scene", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            auto ready = scene;
            ready.recognizer_id = "retry.ready";
            auto result_probe = scene;
            result_probe.recognizer_id = "result";
            workflow::Definition menu_root;
            menu_root.id = "root";
            menu_root.entry = "input";
            workflow::Input action{scene, scene, {{"kind", "Click"}}, {0, 0, 900, 1600}};
            if (std::string(mode) != "non_repeatable") action.retry = workflow::InputRetry{ready, 1s};
            menu_root.steps.emplace("input", step("input", action, {"await"}));
            menu_root.steps.emplace("await", step("await", workflow::AwaitResult{result_probe, 2200ms, 0ms, 10ms}, {"finish"}));
            menu_root.steps.emplace("finish", step("finish", workflow::Finish{}));
            if (std::string(mode) == "no_progress") {
                menu_root.steps.at("await").on_error = {"local_recovery"};
                menu_root.steps.emplace("local_recovery", step("local_recovery", workflow::Fail{"local_recovery_reached"}));
            }
            menu.definitions.emplace("root", std::move(menu_root));
            MenuRetryPorts menu_ports;
            menu_ports.mode = mode;
            runtime::FlowExecutor menu_executor(menu, menu_ports, 3s);
            runtime::TickResult end;
            for (int i = 0; i < 350; ++i) {
                end = menu_executor.tick();
                if (end.state == runtime::TickState::Waiting) std::this_thread::sleep_until(end.wake_at);
                if (end.state != runtime::TickState::Progress && end.state != runtime::TickState::Waiting) break;
            }
            const bool repeats = std::string(mode) == "retry" || std::string(mode) == "changing_hint";
            if (std::string(mode) == "no_progress") {
                if (end.state != runtime::TickState::Failed || end.code != "local_recovery_reached" ||
                    menu_ports.epoch < 2 || menu_executor.has_unresolved_input())
                    throw std::runtime_error("RETRY_LOCAL_RECOVERY_FAILED:" + end.code);
                continue;
            }
            if (repeats ? (end.state != runtime::TickState::Completed || menu_ports.epoch != 2 || menu_executor.has_unresolved_input())
                        : (end.state != runtime::TickState::ExternalBlocked || menu_ports.epoch != 1 || !menu_executor.has_unresolved_input()))
                throw std::runtime_error(std::string("MENU_RETRY_CONTRACT_FAILED:") + mode + ":" + end.code);
        }
        std::cout << "tolerance: progress/stall and six input retry contracts passed\n";
        if (argc == 2 && std::string(argv[1]) == "--tolerance") return 0;
        workflow::FlowProgram program;
        program.revision = "test-1";
        program.root_definition = "root";
        workflow::Definition root;
        root.id = "root";
        root.entry = "operation";
        root.steps.emplace("operation", step("operation",
            workflow::RegisteredOperation{"WvdCombat", nlohmann::json::object()}, {"finish"}));
        root.steps.at("operation").guard = recognition::Request{
            "test.guard", "1", {0, 0, 900, 1600},
            recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
        root.steps.emplace("finish", step("finish", workflow::Finish{}));
        program.definitions.emplace("root", std::move(root));
        Ports ports;
        runtime::FlowExecutor executor(program, ports, std::chrono::seconds{5});
        runtime::TickResult result;
        for (int i = 0; i < 8; ++i) {
            result = executor.tick();
            if (result.state == runtime::TickState::Completed ||
                result.state == runtime::TickState::Failed) break;
        }
        if (result.state != runtime::TickState::Completed ||
            ports.operations != 2 || ports.first_operation_frame == 0 ||
            ports.second_operation_frame <= ports.first_operation_frame)
            throw std::runtime_error("WAIT_REUSED_STALE_FRAME:" + result.code);

        workflow::FlowProgram ambiguous;
        ambiguous.revision = "test-2";
        ambiguous.root_definition = "root";
        workflow::Definition route;
        route.id = "root";
        route.entry = "route";
        auto entry = step("route", workflow::Route{{"finish"}}, {"finish"});
        for (const auto *id : {"network", "maintenance"}) {
            workflow::EventRule event;
            event.id = id;
            event.category = workflow::EventClass::Overlay;
            event.priority = 10;
            event.ambiguity_budget = std::chrono::milliseconds{1};
            event.disposition = workflow::EventDisposition::ExternalBlocked;
            event.reason = id;
            event.detect = recognition::Request{std::string("event.") + id, "1",
                {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            entry.event_policy.push_back(std::move(event));
        }
        route.steps.emplace("route", std::move(entry));
        route.steps.emplace("finish", step("finish", workflow::Finish{}));
        ambiguous.definitions.emplace("root", std::move(route));
        Ports event_ports;
        runtime::FlowExecutor event_executor(ambiguous, event_ports,
                                             std::chrono::seconds{5});
        event_executor.tick();
        if (event_executor.tick().state != runtime::TickState::Waiting)
            throw std::runtime_error("AMBIGUOUS_EVENT_CHOSEN");
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        const auto ambiguous_result = event_executor.tick();
        if (ambiguous_result.state != runtime::TickState::Failed ||
            ambiguous_result.code != "EVENT_AMBIGUOUS")
            throw std::runtime_error("AMBIGUOUS_EVENT_NOT_BOUNDED");

        workflow::FlowProgram recovery;
        recovery.revision = "test-3";
        recovery.root_definition = "root";
        workflow::Definition recover_root;
        recover_root.id = "root";
        recover_root.entry = "operation";
        auto recover_operation = step("operation",
            workflow::RegisteredOperation{"WvdCombat", nlohmann::json::object()});
        recover_operation.on_error = {"recovery"};
        recover_root.steps.emplace("operation", std::move(recover_operation));
        recover_root.steps.emplace("recovery", step("recovery",
            workflow::Fail{"RECOVERY_REACHED"}));
        recovery.definitions.emplace("root", std::move(recover_root));
        Ports recovery_ports;
        recovery_ports.fail_operation = true;
        runtime::FlowExecutor recovery_executor(recovery, recovery_ports,
                                                std::chrono::seconds{5});
        runtime::TickResult recovered;
        for (int i = 0; i < 4; ++i) {
            recovered = recovery_executor.tick();
            if (recovered.state == runtime::TickState::Failed) break;
        }
        if (recovered.code != "RECOVERY_REACHED")
            throw std::runtime_error("ERROR_ROUTE_NOT_REACHED:" + recovered.code);

        const auto inherited_event = [] {
            workflow::EventRule event;
            event.id = "network";
            event.category = workflow::EventClass::Overlay;
            event.priority = 100;
            event.disposition = workflow::EventDisposition::ExternalBlocked;
            event.reason = "PARENT_NETWORK";
            event.detect = recognition::Request{"event.network", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            return event;
        };
        const auto nested_program = [&](bool disable, bool override_rule) {
            workflow::FlowProgram nested;
            nested.revision = "event-scope";
            nested.root_definition = "root";
            workflow::Definition parent;
            parent.id = "root";
            parent.entry = "call";
            auto call = step("call", workflow::Call{"child"}, {"finish"});
            call.event_policy.push_back(inherited_event());
            parent.steps.emplace("call", std::move(call));
            parent.steps.emplace("finish", step("finish", workflow::Finish{}));
            nested.definitions.emplace("root", std::move(parent));
            workflow::Definition child;
            child.id = "child";
            child.entry = "route";
            auto route_step = step("route", workflow::Route{{"return"}}, {"return"});
            if (disable) route_step.disabled_events.insert("network");
            if (override_rule) {
                auto event = inherited_event();
                event.reason = "CHILD_NETWORK";
                route_step.event_policy.push_back(std::move(event));
            }
            child.steps.emplace("route", std::move(route_step));
            child.steps.emplace("return", step("return", workflow::Return{"completed"}));
            nested.definitions.emplace("child", std::move(child));
            return nested;
        };
        for (const auto &[disable, override_rule, expected, captures] :
             std::vector<std::tuple<bool, bool, std::string, int>>{
                 {false, false, "PARENT_NETWORK", 1},
                 {false, true, "CHILD_NETWORK", 1},
                 {true, false, "PARENT_NETWORK", 2}}) {
            auto nested = nested_program(disable, override_rule);
            Ports nested_ports;
            runtime::FlowExecutor nested_executor(nested, nested_ports,
                                                  std::chrono::seconds{5});
            runtime::TickResult outcome;
            for (int i = 0; i < 8; ++i) {
                outcome = nested_executor.tick();
                if (outcome.state == runtime::TickState::Completed ||
                    outcome.state == runtime::TickState::ExternalBlocked ||
                    outcome.state == runtime::TickState::Failed) break;
            }
            const auto actual = outcome.state == runtime::TickState::Completed ?
                std::string("COMPLETED") : outcome.code;
            if (actual != expected || nested_ports.captures != captures)
                throw std::runtime_error("EVENT_SCOPE_MISMATCH:" + actual + ":" + expected +
                    ":captures=" + std::to_string(nested_ports.captures) + ":expected=" + std::to_string(captures));
        }
        workflow::FlowProgram exit_program;
        exit_program.revision = "event-exit-budget";
        exit_program.root_definition = "root";
        workflow::Definition exit_root;
        exit_root.id = "root";
        exit_root.entry = "route";
        auto exit_route = step("route", workflow::Route{{"finish"}}, {"finish"});
        exit_route.time_limit = 60ms;
        workflow::EventRule exit_rule = inherited_event();
        exit_rule.disposition = workflow::EventDisposition::Handle;
        exit_rule.handler_definition = "handler";
        exit_rule.exit_budget = 500ms;
        exit_route.event_policy.push_back(exit_rule);
        exit_root.steps.emplace("route", std::move(exit_route));
        exit_root.steps.emplace("finish", step("finish", workflow::Finish{}));
        exit_program.definitions.emplace("root", std::move(exit_root));
        workflow::Definition exit_handler;
        exit_handler.id = "handler";
        exit_handler.entry = "return";
        exit_handler.steps.emplace("return", step("return", workflow::Return{"completed"}));
        exit_program.definitions.emplace("handler", std::move(exit_handler));
        ExitPorts exit_ports;
        runtime::FlowExecutor exit_executor(exit_program, exit_ports, 2s);
        runtime::TickResult exit_result;
        for (int i = 0; i < 12; ++i) {
            exit_result = exit_executor.tick();
            if (exit_result.state == runtime::TickState::Waiting)
                std::this_thread::sleep_until(exit_result.wake_at);
            if (exit_result.state == runtime::TickState::Completed ||
                exit_result.state == runtime::TickState::Failed) break;
        }
        if (exit_result.state != runtime::TickState::Completed ||
            exit_ports.event_probes != 5 || exit_ports.captures != 4)
            throw std::runtime_error("EVENT_EXIT_CONSUMED_PARENT_BUDGET:" + exit_result.code +
                ":state=" + std::to_string(static_cast<int>(exit_result.state)) +
                ":probes=" + std::to_string(exit_ports.event_probes) +
                ":captures=" + std::to_string(exit_ports.captures));
        workflow::FlowProgram layers;
        layers.revision = "nested-event-exits";
        layers.root_definition = "root";
        workflow::Definition layer_root;
        layer_root.id = "root";
        layer_root.entry = "route";
        auto layer_route = step("route", workflow::Route{{"finish"}}, {"finish"});
        layer_route.time_limit = 100ms;
        for (const auto &[id, category, priority] :
             std::vector<std::tuple<std::string, workflow::EventClass, int>>{
                 {"network", workflow::EventClass::Overlay, 100},
                 {"chest", workflow::EventClass::Encounter, 20}}) {
            workflow::EventRule event;
            event.id = id;
            event.category = category;
            event.priority = priority;
            event.disposition = workflow::EventDisposition::Handle;
            event.handler_definition = id;
            event.exit_budget = 500ms;
            event.detect = recognition::Request{"event." + id, "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            layer_route.event_policy.push_back(std::move(event));
            workflow::Definition handler;
            handler.id = id;
            handler.entry = id + "-return";
            handler.steps.emplace(handler.entry,
                step(handler.entry, workflow::Return{"completed"}));
            layers.definitions.emplace(id, std::move(handler));
        }
        layer_root.steps.emplace("route", std::move(layer_route));
        auto guarded_finish = step("finish", workflow::Finish{});
        guarded_finish.guard = recognition::Request{"candidate.finish", "1",
            {0, 0, 900, 1600},
            recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
        layer_root.steps.emplace("finish", std::move(guarded_finish));
        layers.definitions.emplace("root", std::move(layer_root));
        LayerPorts layer_ports;
        runtime::FlowExecutor layer_executor(layers, layer_ports, 2s);
        runtime::TickResult layer_result;
        int chest_calls = 0, network_calls = 0;
        for (int i = 0; i < 20; ++i) {
            layer_result = layer_executor.tick();
            if (layer_executor.current_step_id() == "chest-return") ++chest_calls;
            if (layer_executor.current_step_id() == "network-return") ++network_calls;
            if (layer_result.state == runtime::TickState::Waiting)
                std::this_thread::sleep_until(layer_result.wake_at);
            if (layer_result.state == runtime::TickState::Completed ||
                layer_result.state == runtime::TickState::Failed) break;
        }
        if (layer_result.state != runtime::TickState::Completed ||
            chest_calls != 1 || network_calls != 1 || layer_ports.captures < 5)
            throw std::runtime_error("NESTED_EVENT_EXIT_LOST:" + layer_result.code);
        workflow::FlowProgram pending_program;
        pending_program.revision = "event-parent-pending";
        pending_program.root_definition = "root";
        recognition::Request probe{"scene", "1", {0, 0, 900, 1600},
            recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
        workflow::Definition pending_root;
        pending_root.id = "root";
        pending_root.entry = "input";
        pending_root.steps.emplace("input", step("input", workflow::Input{probe, probe,
            {{"kind", "Click"}}, {0, 0, 900, 1600}, std::nullopt, true, false}, {"await"}));
        auto await_step = step("await", workflow::AwaitResult{probe, 2s, 0ms, 10ms}, {"finish"});
        workflow::EventRule pending_event;
        pending_event.id = "network";
        pending_event.category = workflow::EventClass::Overlay;
        pending_event.priority = 10;
        pending_event.disposition = workflow::EventDisposition::Handle;
        pending_event.handler_definition = "handler";
        pending_event.detect = recognition::Request{"event.network", "1", {0, 0, 900, 1600},
            recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
        await_step.event_policy.push_back(std::move(pending_event));
        pending_root.steps.emplace("await", std::move(await_step));
        pending_root.steps.emplace("finish", step("finish", workflow::Finish{}));
        pending_program.definitions.emplace("root", std::move(pending_root));
        workflow::Definition pending_handler;
        pending_handler.id = "handler";
        pending_handler.entry = "return";
        pending_handler.steps.emplace("return", step("return", workflow::Return{"completed"}));
        pending_program.definitions.emplace("handler", std::move(pending_handler));
        PendingEventPorts pending_ports;
        runtime::FlowExecutor pending_executor(pending_program, pending_ports, 3s);
        auto pending_result = pending_executor.tick();
        if (pending_result.state != runtime::TickState::Progress || !pending_executor.has_unresolved_input())
            throw std::runtime_error("PARENT_INPUT_NOT_PENDING");
        pending_result = pending_executor.tick();
        pending_result = pending_executor.tick();
        if (pending_executor.invocation_depth() != 2)
            throw std::runtime_error("PENDING_EVENT_NOT_ENTERED:" + pending_result.code +
                ":step=" + pending_executor.current_step_id() +
                ":captures=" + std::to_string(pending_ports.captures) +
                ":seen=" + pending_ports.seen);
        pending_result = pending_executor.tick();
        if (pending_result.state != runtime::TickState::Progress ||
            pending_executor.invocation_depth() != 1 || !pending_executor.has_unresolved_input())
            throw std::runtime_error("PARENT_PENDING_LOST_ON_RETURN:" + pending_result.code);
        for (int i = 0; i < 8; ++i) {
            pending_result = pending_executor.tick();
            if (pending_result.state == runtime::TickState::Waiting)
                std::this_thread::sleep_until(pending_result.wake_at);
            if (pending_result.state == runtime::TickState::Completed ||
                pending_result.state == runtime::TickState::Failed) break;
        }
        if (pending_result.state != runtime::TickState::Completed ||
            pending_executor.has_unresolved_input() || pending_ports.epoch != 1)
            throw std::runtime_error("PARENT_PENDING_NOT_CONFIRMED:" + pending_result.code);

        // 同一正式步骤：正常结果不扫异常；不符先异常再特殊；处理后原回执完成且不重发。
        auto mismatch_program = pending_program;
        auto &mismatch_await = mismatch_program.definitions.at("root").steps.at("await");
        mismatch_await.check_group = "combat";
        std::get<workflow::AwaitResult>(mismatch_await.data).budget = 80ms;
        auto &network_rule = mismatch_await.event_policy.front();
        network_rule.category = workflow::EventClass::Exception;
        network_rule.priority = 1000;
        const auto diagnostic_rule = network_rule;
        for (const auto &[id, category, priority] : {
                std::tuple{"lower", workflow::EventClass::Exception, 800},
                std::tuple{"special", workflow::EventClass::Special, 400}}) {
            auto rule = diagnostic_rule;
            rule.id = id;
            rule.category = category;
            rule.priority = priority;
            rule.detect.recognizer_id = "event." + std::string(id);
            mismatch_await.event_policy.push_back(std::move(rule));
        }
        auto &mismatch_handler = mismatch_program.definitions.at("handler");
        mismatch_handler.entry = "handle";
        mismatch_handler.steps.emplace("handle", step("handle",
            workflow::RegisteredOperation{"Recovery", nlohmann::json::object()}, {"return"}));
        for (const auto mode : {"normal", "animation", "interrupted", "before_input", "unknown"}) {
            MismatchPorts dispatch_ports;
            dispatch_ports.animation = std::string(mode) == "animation";
            dispatch_ports.before_input = std::string(mode) == "before_input";
            dispatch_ports.interrupted = std::string(mode) != "normal" && !dispatch_ports.animation;
            dispatch_ports.unknown = std::string(mode) == "unknown";
            auto scenario = mismatch_program;
            if (dispatch_ports.before_input) {
                auto &root = scenario.definitions.at("root");
                root.entry = "choose";
                root.steps.emplace("choose", step("choose", workflow::Route{}, {"input", "never"}));
                auto guarded_input = probe;
                guarded_input.recognizer_id = "candidate";
                root.steps.at("input").guard = guarded_input;
                root.steps.at("input").event_policy = mismatch_await.event_policy;
                auto never_probe = probe;
                never_probe.recognizer_id = "never";
                root.steps.emplace("never", step("never", workflow::Observe{never_probe}, {"finish"}));
                root.steps.at("never").guard = never_probe;
            }
            runtime::FlowExecutor dispatch(scenario, dispatch_ports, 3s);
            runtime::TickResult end;
            for (int i = 0; i < 100; ++i) {
                end = dispatch.tick();
                if (end.state == runtime::TickState::Waiting) std::this_thread::sleep_until(end.wake_at);
                if (end.state != runtime::TickState::Waiting && end.state != runtime::TickState::Progress) break;
            }
            if (dispatch_ports.epoch != 1) throw std::runtime_error("DISPATCH_REPLAYED_INPUT");
            if (std::string(mode) == "normal" || dispatch_ports.animation) {
                if (end.state != runtime::TickState::Completed || dispatch_ports.exception_probes ||
                    dispatch_ports.special_probes || dispatch_ports.lower_probes || dispatch_ports.handler_calls)
                    throw std::runtime_error("NORMAL_COMBAT_SCANNED_DIAGNOSTICS:" + end.code);
            } else if (std::string(mode) == "interrupted" || dispatch_ports.before_input) {
                if (end.state != runtime::TickState::Completed || dispatch_ports.handler_calls != 1 ||
                    dispatch_ports.lower_probes || dispatch_ports.special_probes || dispatch.has_unresolved_input())
                    throw std::runtime_error("MISMATCH_RECOVERY_OR_PRIORITY_FAILED:" + end.code);
            } else if (end.state != runtime::TickState::ExternalBlocked ||
                dispatch_ports.handler_calls || !dispatch_ports.exception_probes || !dispatch_ports.special_probes ||
                !dispatch.has_unresolved_input())
                throw std::runtime_error("UNKNOWN_RESULT_WAS_REPLAYED_OR_SUCCEEDED:" + end.code);
        }
        std::cout << "flow reobservation, event scope, exit budget and menu retry passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
