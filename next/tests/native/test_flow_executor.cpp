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
        if (argc == 2 && std::string(argv[1]) == "--recovery-recheck") {
            for (const std::string mode : {"cleared", "still_present", "recognition_error", "unconditional"}) {
                struct RecoveryPorts final : Ports {
                    bool handled{}, still_present{}, recognition_error{};
                    int handler_calls{}, fresh_checks{};
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                        const recognition::Request &request) override {
                        auto result = Ports::recognize(frame, request);
                        if (request.recognizer_id == "network" && handled)
                            result.outcome = contracts::RecognitionOutcome::NoHit;
                        if (request.recognizer_id == "failure" && handled) {
                            ++fresh_checks;
                            result.outcome = recognition_error ? contracts::RecognitionOutcome::Error :
                                still_present ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
                            if (recognition_error) result.error_code = "RECHECK_BROKEN";
                        }
                        return result;
                    }
                    runtime::OperationResult operate(const std::string &, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                        const std::string &) override {
                        handled = true; ++handler_calls;
                        return {runtime::OperationState::Done};
                    }
                } ports;
                ports.still_present = mode == "still_present";
                ports.recognition_error = mode == "recognition_error";
                recognition::Request guard{"failure", "1", {0,0,900,1600},
                    recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                workflow::FlowProgram program; program.revision = "recovery-recheck"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "failure";
                auto failure = step("failure", workflow::Fail{"ORIGINAL_FAILURE"});
                if (mode != "unconditional") { failure.guard = guard; failure.next = {"done"}; }
                workflow::EventRule event;
                event.id = "network"; event.category = workflow::EventClass::Exception;
                event.detect = guard; event.detect.recognizer_id = "network";
                event.handler_definition = "handler"; event.resume = workflow::ResumeMode::Reobserve;
                failure.event_policy.push_back(event);
                root.steps.emplace("failure", std::move(failure));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                program.definitions.emplace("root", std::move(root));
                workflow::Definition handler; handler.id = "handler"; handler.entry = "handle";
                handler.steps.emplace("handle", step("handle", workflow::RegisteredOperation{"NetworkRetry", nlohmann::json::object()}, {"return"}));
                handler.steps.emplace("return", step("return", workflow::Return{"completed"}));
                program.definitions.emplace("handler", std::move(handler));
                program.validate();
                if (mode == "cleared") {
                    for (const bool remove_guard : {false, true}) {
                        auto invalid = program;
                        auto &bad = invalid.definitions.at("root").steps.at("failure");
                        if (remove_guard) bad.guard.reset(); else bad.next.clear();
                        bool rejected = false;
                        try { invalid.validate(); } catch (const std::exception &error) {
                            rejected = std::string(error.what()) == "FLOW_RECOVERY_RECHECK_INVALID";
                        }
                        if (!rejected) throw std::runtime_error("INCOMPLETE_RECOVERY_CONTRACT_ACCEPTED");
                    }
                }
                runtime::FlowExecutor executor(program, ports, 3s);
                runtime::TickResult result;
                for (int i = 0; i < 100; ++i) {
                    result = executor.tick();
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                }
                const auto expected = mode == "cleared" ? runtime::TickState::Completed : runtime::TickState::Failed;
                if (result.state != expected || ports.handler_calls != 1 ||
                    (mode != "unconditional" && ports.fresh_checks == 0) || executor.has_unresolved_input() ||
                    (mode == "recognition_error" && result.code != "RECHECK_BROKEN") ||
                    ((mode == "still_present" || mode == "unconditional") && result.code != "ORIGINAL_FAILURE"))
                    throw std::runtime_error("RECOVERY_RECHECK_FAILED:" + mode + ":" + result.code);
            }
            std::cout << "PASS event-return recheck: cleared resumes, persistent/error/unconditional fail, no input\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--linkage-closure") {
            // Uses the production FlowExecutor. Only pixels, device and recognition leaves
            // are replaced; no real game input or service is opened by this entry.
            const auto check = [](bool ok, const std::string &reason) {
                if (!ok) throw std::runtime_error("LINKAGE_CLOSURE:" + reason);
            };
            const auto request = [](const char *id) {
                return recognition::Request{id, "1", {0, 0, 900, 1600},
                    recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            };
            const auto drive = [&](runtime::FlowExecutor &executor) {
                runtime::TickResult result;
                for (int i = 0; i < 2000; ++i) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting)
                        return result;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                throw std::runtime_error("LINKAGE_CLOSURE:bounded driver exhausted");
            };
            const auto add_boot = [&](workflow::FlowProgram &program, workflow::Definition &root) {
                workflow::Definition boot; boot.id = "boot"; boot.entry = "marker";
                boot.steps.emplace("marker", step("marker", workflow::RegisteredOperation{"BootMarker", nlohmann::json::object()}, {"return"}));
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                program.definitions.emplace("boot", std::move(boot));
                workflow::EventRule event; event.id = "test-restart"; event.detect = request("never");
                event.handler_definition = "boot"; event.on_device_restart = true;
                root.events.push_back(std::move(event));
            };
            for (const bool single : {false, true}) {
                struct HandoffPorts final : Ports {
                    bool single{};
                    int a_checks{}, child_calls{};
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                        const recognition::Request &r) override {
                        auto result = Ports::recognize(frame, r);
                        bool hit = false;
                        if (r.recognizer_id == "a") { ++a_checks; hit = a_checks == 1 || (single && a_checks >= 3); }
                        if (r.recognizer_id == "b") hit = a_checks >= 2;
                        if (!hit) result.outcome = contracts::RecognitionOutcome::NoHit;
                        return result;
                    }
                    runtime::OperationResult operate(const std::string &, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                        const std::string &) override { ++child_calls; return {runtime::OperationState::Done}; }
                } ports;
                ports.single = single;
                workflow::FlowProgram program; program.root_definition = "root"; program.revision = "handoff-source";
                workflow::Definition root; root.id = "root"; root.entry = "call";
                root.steps.emplace("call", step("call", workflow::Call{"child", {{"route", single ? std::vector<std::string>{"a"} : std::vector<std::string>{"a", "b"}}}}, {"wrong"}));
                for (const auto *id : {"a", "b"}) {
                    auto node = step(id, workflow::Observe{request(id)}, {"done"}); node.guard = request(id);
                    root.steps.emplace(id, std::move(node));
                }
                root.steps.emplace("wrong", step("wrong", workflow::ExternalBlocked{"wrong_success_continuation"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition child; child.id = "child"; child.entry = "marker"; child.handoffs.insert("route");
                child.steps.emplace("marker", step("marker", workflow::RegisteredOperation{"ChildMarker", nlohmann::json::object()}, {"return"}));
                child.steps.emplace("return", step("return", workflow::Return{"handoff", "", "route"}));
                program.definitions.emplace("root", std::move(root)); program.definitions.emplace("child", std::move(child));
                runtime::FlowExecutor executor(program, ports, 3s);
                bool observed_retraction = false;
                for (int i = 0; i < 100 && ports.a_checks < 2; ++i) {
                    const auto result = executor.tick();
                    check(result.state == runtime::TickState::Progress || result.state == runtime::TickState::Waiting, "handoff ended before race");
                    if (ports.a_checks == 2) observed_retraction = executor.current_step_id() == "call";
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                check(observed_retraction, "handoff race did not restore caller selection");
                const auto result = drive(executor);
                check(result.state == runtime::TickState::Completed && ports.child_calls == 1 && !executor.has_unresolved_input(), "handoff became success/replayed child:" + result.code);
                std::cout << "linkage handoff " << (single ? "single" : "multiple") << " PASS\n";
            }
            for (const std::string mode : {"arrival", "stopped", "stalled", "progress", "unknown", "error", "stale", "cancel", "total", "stalled-threshold"}) {
                struct PollPorts final : Ports {
                    std::string mode;
                    bool stopped{}, restored{}, arrival_ready{}, changed{}, scene_known{true};
                    int restarts{}, boots{};
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                        const recognition::Request &r) override {
                        auto result = Ports::recognize(frame, r);
                        bool hit = false;
                        if (r.recognizer_id == "arrival") hit = mode == "arrival" || arrival_ready || restored;
                        if (r.recognizer_id == "stopped") hit = mode == "stopped";
                        if (r.recognizer_id == "scene") hit = scene_known || restored;
                        if (r.recognizer_id == "changed") hit = changed;
                        if (!hit) result.outcome = contracts::RecognitionOutcome::NoHit;
                        if (mode == "error" && r.recognizer_id == "arrival") {
                            result.outcome = contracts::RecognitionOutcome::Error; result.error_code = "TEST_POLL_RESOURCE_ERROR";
                        }
                        if (mode == "stale") ++result.basis.action_epoch;
                        return result;
                    }
                    contracts::ObservationRecovery recover_observation(bool force = false) override {
                        if (force) ++restarts;
                        restored = true;
                        return {{}, true, true};
                    }
                    runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                        const std::string &) override {
                        if (binding == "BootMarker") ++boots;
                        return {runtime::OperationState::Done};
                    }
                    bool cancelled() const override { return stopped; }
                } ports;
                ports.mode = mode; ports.changed = mode == "progress";
                ports.scene_known = mode != "unknown" && mode != "stalled-threshold";
                workflow::FlowProgram program; program.root_definition = "root"; program.revision = "expired-poll";
                workflow::Definition root; root.id = "root"; root.entry = "moving";
                auto moving = step("moving", workflow::Poll{1ms, request("scene"), request("changed")}, {"arrival", "stopped", "moving"});
                moving.max_hit = 0; moving.time_limit = 40ms; moving.on_error = {"stall"};
                root.steps.emplace("moving", std::move(moving));
                for (const auto *id : {"arrival", "stopped"}) {
                    auto node = step(id, workflow::Observe{request(id)}, {"done"}); node.guard = request(id);
                    root.steps.emplace(id, std::move(node));
                }
                root.steps.emplace("stall", step("stall", workflow::ExternalBlocked{"TEST_POLL_NO_PROGRESS"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                add_boot(program, root); program.definitions.emplace("root", std::move(root));
                runtime::FlowExecutor executor(program, ports, mode == "total" ? 50ms : 3s, {1000ms, 1ms, 5ms, 60ms});
                if (mode == "stalled-threshold") {
                    for (int i = 0; i < 100 && !executor.progress_snapshot().at("continuous_exception").at("active").get<bool>(); ++i) {
                        const auto r = executor.tick();
                        check(r.state == runtime::TickState::Progress || r.state == runtime::TickState::Waiting, "threshold priming failed");
                        if (r.state == runtime::TickState::Waiting) std::this_thread::sleep_until(r.wake_at);
                    }
                    check(executor.progress_snapshot().at("continuous_exception").at("active").get<bool>(), "exception clock not armed");
                    ports.scene_known = true;
                }
                std::this_thread::sleep_for(80ms); // Compress test budget, never change production thresholds.
                if (mode == "cancel") ports.stopped = true;
                if (mode == "progress") {
                    const auto first = executor.tick();
                    check(first.state == runtime::TickState::Waiting && executor.current_step_id() == "moving", "declared progress not accepted");
                    ports.changed = false; ports.arrival_ready = true;
                }
                const auto result = drive(executor);
                if (mode == "stalled" || mode == "stalled-threshold")
                    check(result.state == runtime::TickState::ExternalBlocked && result.code == "TEST_POLL_NO_PROGRESS", "explicit error edge lost:" + result.code);
                else if (mode == "error") check(result.state == runtime::TickState::Failed && result.code == "TEST_POLL_RESOURCE_ERROR", "recognition error swallowed");
                else if (mode == "stale") check(result.state == runtime::TickState::Failed && result.code == "EXPIRED_POLL_EVIDENCE_STALE", "stale evidence accepted");
                else if (mode == "cancel") check(result.state == runtime::TickState::Cancelled, "cancel overridden");
                else if (mode == "total") check(result.state == runtime::TickState::Failed && result.code == "FLOW_TOTAL_DEADLINE", "hard deadline overridden");
                else check(result.state == runtime::TickState::Completed, "late/normal result lost:" + mode + ":" + result.code);
                check(ports.restarts == (mode == "unknown" ? 1 : 0) && !executor.has_unresolved_input(), "unexpected restart or pending");
                std::cout << "linkage poll " << mode << " PASS\n";
            }
            {
                // A repeatable menu chosen from a handoff is interrupted by app restart.
                // After Boot, only the ORIGINAL handoff may select its replacement menu.
                struct RestartPorts final : Ports {
                    std::uint64_t epoch{};
                    int child_calls{}, boots{};
                    bool restored{};
                    contracts::FrameEnvelope capture() override {
                        if (epoch && !restored)
                            throw contracts::ObservationUnavailable({contracts::ReadFaultKind::ApplicationUnavailable,
                                contracts::ReadFaultStage::Capture, "GAME_NOT_FOREGROUND", "test.capture", {}, {}});
                        auto frame = Ports::capture(); frame.identity.action_epoch = epoch;
                        frame.identity.raw_size = {900, 1600}; return frame;
                    }
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame, const recognition::Request &r) override {
                        auto result = Ports::recognize(frame, r);
                        const bool hit = r.recognizer_id == "old" ? !restored : r.recognizer_id == "new" ? restored :
                            r.recognizer_id == "result" ? epoch == 2 : false;
                        if (!hit) result.outcome = contracts::RecognitionOutcome::NoHit;
                        result.action_eligible = true; result.box = contracts::Box{100,100,20,20}; result.center = contracts::Point{110,110};
                        return result;
                    }
                    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
                        const contracts::Observation &, contracts::Box, const std::string &) override {
                        return {runtime::SubmissionState::Accepted, ++epoch, std::chrono::steady_clock::now(), {}};
                    }
                    contracts::ObservationRecovery recover_observation(bool = false) override { restored = true; return {{},true,true}; }
                    runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &, const std::string &) override {
                        if (binding == "ChildMarker") ++child_calls;
                        if (binding == "BootMarker") ++boots;
                        return {runtime::OperationState::Done};
                    }
                } ports;
                workflow::FlowProgram program; program.root_definition = "root"; program.revision = "restart-handoff";
                workflow::Definition root; root.id = "root"; root.entry = "call";
                root.steps.emplace("call", step("call", workflow::Call{"child", {{"route", {"old", "new"}}}}, {"wrong"}));
                for (const auto *id : {"old", "new"}) {
                    workflow::Input input{request(id), request(id), {{"kind","Click"}}, {0,0,900,1600}};
                    input.retry = workflow::InputRetry{request(id), 1s, 2};
                    auto node = step(id, std::move(input), {std::string(id) + "-await"}); node.guard = request(id);
                    root.steps.emplace(id, std::move(node));
                    root.steps.emplace(std::string(id)+"-await", step(std::string(id)+"-await", workflow::AwaitResult{request("result"),2s,0ms,1ms}, {"done"}));
                }
                root.steps.emplace("wrong", step("wrong", workflow::ExternalBlocked{"wrong_success_continuation"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition child; child.id = "child"; child.entry = "marker"; child.handoffs.insert("route");
                child.steps.emplace("marker", step("marker", workflow::RegisteredOperation{"ChildMarker",nlohmann::json::object()}, {"return"}));
                child.steps.emplace("return", step("return", workflow::Return{"handoff","","route"}));
                add_boot(program,root); program.definitions.emplace("root",std::move(root));program.definitions.emplace("child",std::move(child));
                runtime::FlowExecutor executor(program,ports,3s);
                const auto result = drive(executor);
                check(result.state == runtime::TickState::Completed && ports.epoch == 2 && ports.child_calls == 1 && ports.boots == 1,
                    "restart menu lost handoff or replayed child:" + result.code);
                std::cout << "linkage restart handoff PASS\n";
            }
            std::cout << "linkage closure: 13 bounded production-executor cases; no real device input\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--child-result-frame") {
            struct ResultPorts final : Ports {
                bool valid{true};
                std::uint64_t child_frame{}, parent_frame{};
                bool reusable(const contracts::FrameIdentity &) const override { return valid; }
                contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                    const recognition::Request &request) override {
                    if (request.recognizer_id == "child.result") child_frame = frame.identity.frame_id;
                    if (request.recognizer_id == "parent.result") parent_frame = frame.identity.frame_id;
                    return Ports::recognize(frame, request);
                }
            };
            for (const bool valid : {true, false}) {
                const auto request = [](const char *id) {
                    return recognition::Request{id, "1", {0, 0, 900, 1600},
                        recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                };
                workflow::FlowProgram program;
                program.revision = "child-result-frame"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "call";
                root.steps.emplace("call", step("call", workflow::Call{"child", {}}, {"check"}));
                root.steps.emplace("check", step("check", workflow::Observe{request("parent.result")}, {"done"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition child; child.id = "child"; child.entry = "observe";
                child.steps.emplace("observe", step("observe", workflow::Observe{request("child.result")}, {"return"}));
                child.steps.emplace("return", step("return", workflow::Return{"completed"}));
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("child", std::move(child));
                ResultPorts ports; ports.valid = valid;
                runtime::FlowExecutor executor(program, ports, 2s);
                runtime::TickResult result;
                for (int i = 0; i < 20; ++i) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Progress) break;
                }
                if (result.state != runtime::TickState::Completed || !ports.child_frame ||
                    (valid ? ports.parent_frame != ports.child_frame : ports.parent_frame <= ports.child_frame))
                    throw std::runtime_error("CHILD_RESULT_FRAME_HANDOFF:" + result.code);
            }
            std::cout << "child result frame retained only while port identity/age gate accepts reuse\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--late-receipt") {
            for (const std::string mode : {"local", "threshold", "black", "stale", "generation",
                "error", "unknown", "cleanup", "cleanup-cancel", "cleanup-total", "cancel", "total", "handler"}) {
                struct LatePorts final : MenuRetryPorts {
                    std::string scenario;
                    bool ready{}, restart_done{}, stop{};
                    int restarts{}, boots{}, confirms{}, cleanup_calls{}, parent_checks{}, handler_checks{};
                    std::vector<nlohmann::json> receipts;
                    contracts::Observation recognize(const contracts::FrameEnvelope &f, const recognition::Request &r) override {
                        auto hit = Ports::recognize(f, r);
                        hit.box = contracts::Box{100,100,20,20}; hit.center = contracts::Point{110,110};
                        hit.action_eligible = true;
                        if (r.recognizer_id == "result") {
                            ++parent_checks;
                            if (!ready && !restart_done) hit.outcome = contracts::RecognitionOutcome::NoHit;
                            else if (scenario == "stale") hit.basis.frame_id = 1;
                            else if (scenario == "generation") hit.basis.connection_generation = 2;
                            else if (scenario == "error") { hit.outcome = contracts::RecognitionOutcome::Error; hit.error_code = "LATE_RESOURCE_ERROR"; }
                        }
                        if (r.recognizer_id == "network" && !(scenario == "handler" && epoch == 1))
                            hit.outcome = contracts::RecognitionOutcome::NoHit;
                        if (r.recognizer_id == "button-result") {
                            ++handler_checks;
                            hit.outcome = ready ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
                        }
                        return hit;
                    }
                    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
                        const contracts::Observation &, contracts::Box, const std::string &) override {
                        return {(scenario == "unknown" || scenario.starts_with("cleanup")) ? runtime::SubmissionState::Unresolved : runtime::SubmissionState::Accepted,
                            ++epoch, std::chrono::steady_clock::now(), {}};
                    }
                    contracts::ObservationRecovery recover_observation(bool force = false) override {
                        if (!force) throw std::runtime_error("LATE_UNEXPECTED_READ_RECOVERY");
                        ++restarts; restart_done = true;
                        return {{}, true, true};
                    }
                    runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                        const std::string &) override {
                        if (binding == "Boot") ++boots;
                        if (binding == "Confirm") ++confirms;
                        return {runtime::OperationState::Done};
                    }
                    bool settle_observed_input() override {
                        ++cleanup_calls;
                        if (scenario == "cleanup-cancel") stop = true;
                        if (scenario == "cleanup-total") std::this_thread::sleep_for(80ms);
                        return scenario != "cleanup";
                    }
                    void input_result(const nlohmann::json &row) noexcept override { receipts.push_back(row); }
                    bool cancelled() const override { return stop; }
                } ports;
                ports.scenario = mode;
                recognition::Request scene{"scene", "1", {0,0,900,1600}, recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                auto result_condition = scene; result_condition.recognizer_id = "result";
                workflow::FlowProgram program; program.revision = "late-receipt"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "click";
                workflow::Input input; input.scene = input.target = scene; input.allowed_area = {0,0,900,1600};
                input.command = {{"kind", "Click"}};
                input.retry = workflow::InputRetry{scene, 1s, 0, {}};
                if (mode == "black" || mode == "handler") input.retry.reset();
                root.steps.emplace("click", step("click", input, {"await"}));
                auto await = step("await", workflow::AwaitResult{result_condition, 10ms, 0ms, 2ms}, {"confirm"});
                await.time_limit = 10ms;
                const bool unknown_case = mode == "unknown" || mode.starts_with("cleanup");
                if (unknown_case) {
                    // 未知送达不授权开启事件/重启宽限；只在原结果窗口核对及清理通道。
                    await.data = workflow::AwaitResult{result_condition, 1s, 0ms, 2ms};
                    await.time_limit = 1s;
                }
                root.steps.emplace("await", std::move(await));
                root.steps.emplace("confirm", step("confirm", workflow::RegisteredOperation{"Confirm", nlohmann::json::object()}, {"done"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition boot; boot.id = "boot"; boot.entry = "boot";
                boot.steps.emplace("boot", step("boot", workflow::RegisteredOperation{"Boot", nlohmann::json::object()}, {"return"}));
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::EventRule restart; restart.id = "restart"; restart.detect = scene;
                restart.handler_definition = "boot"; restart.on_device_restart = true;
                root.events.push_back(restart);
                if (mode == "handler") {
                    auto detect = scene; detect.recognizer_id = "network";
                    auto button_result = scene; button_result.recognizer_id = "button-result";
                    workflow::Definition handler; handler.id = "network"; handler.entry = "button";
                    handler.steps.emplace("button", step("button", input, {"button-await"}));
                    handler.steps.emplace("button-await", step("button-await", workflow::AwaitResult{button_result, 1s, 0ms, 2ms}, {"return"}));
                    handler.steps.emplace("return", step("return", workflow::Return{"completed"}));
                    workflow::EventRule rule; rule.id = "network"; rule.detect = detect; rule.handler_definition = "network";
                    rule.category = workflow::EventClass::Exception; rule.resume = workflow::ResumeMode::Reobserve;
                    root.events.push_back(rule);
                    root.checks = workflow::Definition::CheckPolicy{};
                    root.checks->debounce = 0ms;
                    program.definitions.emplace("network", std::move(handler));
                }
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("boot", std::move(boot));
                runtime::FlowExecutor executor(program, ports, mode == "total" ? 30ms : mode == "cleanup-total" ? 300ms : 2s, {200ms, 2ms, 5ms, 80ms});
                runtime::TickResult result;
                bool saw_exception = false, saw_grace = false;
                for (int i = 0; i < 500; ++i) {
                    if (unknown_case && ports.epoch) ports.ready = true;
                    const auto snapshot = executor.progress_snapshot();
                    const auto &exception = snapshot.at("continuous_exception");
                    if (exception.at("active").get<bool>()) {
                        saw_exception = true;
                        const auto elapsed = exception.at("elapsed_ms").get<int>();
                        if (mode == "cancel") ports.stop = true;
                        if (mode != "black" && elapsed >= (mode == "threshold" || mode == "handler" ? 80 : 20)) ports.ready = true;
                        if (elapsed >= 20) saw_grace = true;
                    }
                    result = executor.tick();
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                const bool invalid = mode == "stale" || mode == "generation" || mode == "error";
                const auto expected = invalid || mode == "total" || mode == "cleanup-total" ? runtime::TickState::Failed :
                    mode == "cancel" || mode == "cleanup-cancel" ? runtime::TickState::Cancelled : mode == "cleanup" ? runtime::TickState::ExternalBlocked : runtime::TickState::Completed;
                if ((!unknown_case && !saw_exception) || (!unknown_case && mode != "total" && mode != "cancel" && !saw_grace) || result.state != expected ||
                    ports.epoch != (mode == "handler" ? 2 : 1) || ports.restarts != (mode == "black" || mode == "handler" ? 1 : 0) ||
                    ports.confirms != (expected == runtime::TickState::Completed ? 1 : 0) ||
                    ((mode.starts_with("cleanup") || invalid || mode == "cancel" || mode == "total") && !executor.has_unresolved_input()))
                    throw std::runtime_error("LATE_RECEIPT:" + mode + ":" + result.code + ":inputs=" + std::to_string(ports.epoch) + ":restarts=" + std::to_string(ports.restarts));
                if ((mode == "total" || mode == "cleanup-total") && result.code != "FLOW_TOTAL_DEADLINE") throw std::runtime_error("LATE_TOTAL_DEADLINE_LOST");
                if (mode == "error" && result.code != "LATE_RESOURCE_ERROR") throw std::runtime_error("LATE_ERROR_SWALLOWED");
                if (unknown_case && ports.cleanup_calls != 1) throw std::runtime_error("LATE_CLEANUP_NOT_ONCE");
                std::map<std::uint64_t, int> confirmed;
                for (const auto &receipt : ports.receipts)
                    if (receipt.at("outcome") == "confirmed") ++confirmed[receipt.at("action_epoch").get<std::uint64_t>()];
                if (expected == runtime::TickState::Completed) {
                    for (std::uint64_t epoch = 1; epoch <= ports.epoch; ++epoch)
                        if (confirmed[epoch] != 1) throw std::runtime_error("LATE_RECEIPT_NOT_SETTLED_ONCE");
                } else if (!confirmed.empty()) throw std::runtime_error("LATE_FALSE_CONFIRMATION");
                std::cout << "late " << mode << " inputs=" << ports.epoch << " restarts=" << ports.restarts << " result=" << result.code << '\n';
            }
            for (const std::string mode : {"observe", "next", "return-next", "ongoing", "read-error", "guard-only"}) {
                struct ReadPorts final : Ports {
                    bool ready{}, stop{}; int restarts{}; std::string mode;
                    contracts::Observation recognize(const contracts::FrameEnvelope &f, const recognition::Request &r) override {
                        auto result = Ports::recognize(f, r);
                        if (r.recognizer_id == "normal") {
                            result.outcome = ready ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
                            if (ready && mode == "read-error") {
                                result.outcome = contracts::RecognitionOutcome::Error; result.error_code = "NORMAL_RESOURCE_ERROR";
                            }
                        }
                        if (r.recognizer_id == "ongoing") result.outcome = ready ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
                        return result;
                    }
                    contracts::ObservationRecovery recover_observation(bool force) override {
                        if (mode == "guard-only" && force) { ++restarts; throw std::runtime_error("NO_NORMAL_RESULT_RESTART_EXPECTED"); }
                        throw std::runtime_error("NORMAL_FALSE_RESTART");
                    }
                    bool cancelled() const override { return stop; }
                } ports;
                ports.mode = mode;
                recognition::Request normal{"normal", "1", {0,0,900,1600}, recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                auto ongoing = normal; ongoing.recognizer_id = "ongoing";
                workflow::FlowProgram program; program.revision = "normal-recheck"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = mode == "next" || mode == "ongoing" ? "route" : "observe";
                if (mode == "return-next") {
                    root.entry = "call";
                    workflow::Call call; call.definition = "child"; call.handoffs["ready"] = {"observe"};
                    root.steps.emplace("call", step("call", std::move(call)));
                    workflow::Definition child; child.id = "child"; child.entry = "return";
                    child.handoffs.insert("ready");
                    child.steps.emplace("return", step("return", workflow::Return{"handoff", {}, "ready"}));
                    program.definitions.emplace("child", std::move(child));
                }
                root.steps.emplace("route", step("route", workflow::Route{}, mode == "ongoing" ? std::vector<std::string>{"observe", "poll"} : std::vector<std::string>{"observe"}));
                auto observed = step("observe", workflow::Observe{normal}, {"done"}); observed.guard = normal;
                if (mode == "guard-only") { auto guard = normal; guard.recognizer_id = "entry-guard"; observed.guard = guard; }
                root.steps.emplace("observe", std::move(observed));
                root.steps.emplace("poll", step("poll", workflow::Poll{2ms, ongoing}, {"observe", "poll"}));
                root.steps.at("poll").max_hit = 0;
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition boot; boot.id = "boot"; boot.entry = "return";
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::EventRule restart; restart.id = "restart"; restart.detect = normal; restart.handler_definition = "boot"; restart.on_device_restart = true;
                root.events.push_back(restart);
                program.definitions.emplace("root", std::move(root)); program.definitions.emplace("boot", std::move(boot));
                runtime::FlowExecutor executor(program, ports, 2s, {200ms, 2ms, 5ms, 80ms});
                runtime::TickResult result; bool recovered{};
                for (int i = 0; i < 500; ++i) {
                    const auto state = executor.progress_snapshot().at("continuous_exception");
                    if (mode != "guard-only" && state.at("active") && state.at("elapsed_ms").get<int>() >= 80) ports.ready = true;
                    result = executor.tick();
                    const auto after = executor.progress_snapshot();
                    const auto recovery = after.at("observation_recovery");
                    // 清除异常窗口并开始读取恢复，不代表已经取得正常业务证据。
                    const bool recovering = recovery.is_object() && recovery.value("active", false);
                    if (!recovering && !ports.restarts && state.at("active") && !after.at("continuous_exception").at("active").get<bool>() &&
                        (result.state == runtime::TickState::Progress || result.state == runtime::TickState::Waiting)) recovered = true;
                    if (mode == "ongoing" && recovered) ports.stop = true;
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                const auto expected = mode == "ongoing" ? runtime::TickState::Cancelled : mode == "read-error" || mode == "guard-only" ? runtime::TickState::Failed : runtime::TickState::Completed;
                if (result.state != expected || (!recovered && mode != "read-error" && mode != "guard-only") || executor.has_unresolved_input() ||
                    (mode == "guard-only" && (recovered || ports.restarts != 1 || result.code != "NO_NORMAL_RESULT_RESTART_EXPECTED")) ||
                    (mode == "read-error" && result.code != "NORMAL_RESOURCE_ERROR")) throw std::runtime_error("NORMAL_RECHECK:" + mode + ":" + result.code);
                std::cout << "normal recheck " << mode << " result=" << result.code << "; no input, restart requests=" << ports.restarts << '\n';
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--critical-recovery") {
            // Uses the production FlowExecutor. All frames, lifecycle operations and inputs are fake.
            // No device, shell, game process or profile is opened by this branch.
            const auto require = [](bool value, const std::string &message) {
                if (!value) throw std::runtime_error(message);
            };
            const auto drive = [](runtime::FlowExecutor &executor) {
                runtime::TickResult result;
                for (int i = 0; i < 1000; ++i) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting)
                        return result;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                throw std::runtime_error("CRITICAL_TEST_TICK_LIMIT");
            };
            recognition::Request scene{"scene", "1", {0,0,900,1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            const std::string protected_reason = "combat.skill_outcome_unconfirmed";
            for (const std::string mode : {"retry", "restart_from", "nested", "unknown", "no_retry"}) {
                struct ProtectedPorts final : Ports {
                    std::uint64_t epoch{};
                    int submitted{}, boots{}, prepares{}, result_reads{}, recoveries{};
                    bool recovered{}, unknown{};
                    contracts::FrameEnvelope capture() override {
                        if (epoch && !recovered)
                            throw contracts::ObservationUnavailable({contracts::ReadFaultKind::ApplicationUnavailable,
                                contracts::ReadFaultStage::Capture, "GAME_NOT_FOREGROUND", "test.capture", {}, {}});
                        auto frame = Ports::capture();
                        frame.identity.raw_size = {900,1600}; frame.identity.action_epoch = epoch;
                        return frame;
                    }
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                                      const recognition::Request &request) override {
                        auto result = Ports::recognize(frame, request);
                        result.box = contracts::Box{100,100,20,20}; result.center = contracts::Point{110,110};
                        result.action_eligible = true;
                        if (request.recognizer_id == "result" && recovered) ++result_reads;
                        return result;
                    }
                    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
                        const contracts::Observation &, contracts::Box, const std::string &) override {
                        ++submitted;
                        return {unknown ? runtime::SubmissionState::Unresolved : runtime::SubmissionState::Accepted,
                            ++epoch, std::chrono::steady_clock::now(), {}};
                    }
                    contracts::ObservationRecovery recover_observation(bool = false) override {
                        ++recoveries; recovered = true; return {{}, true, true};
                    }
                    runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                        const std::string &) override {
                        if (binding == "Boot") ++boots;
                        if (binding == "Prepare") ++prepares;
                        return {runtime::OperationState::Done};
                    }
                } ports;
                ports.unknown = mode == "unknown";
                workflow::FlowProgram program; program.revision = "critical-protected"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "dispatch";
                workflow::Input input{scene, scene, {{"kind","Click"}}, {0,0,900,1600}};
                input.interruption_reason = protected_reason;
                if (mode != "no_retry") input.retry = workflow::InputRetry{scene, 1s, 0};
                if (mode == "restart_from") input.retry->restart_from = "dispatch";
                auto result_probe = scene; result_probe.recognizer_id = "result";
                root.steps.emplace("dispatch", step("dispatch", workflow::Route{}, {"input"}));
                root.steps.emplace("input", step("input", std::move(input), {"await"}));
                root.steps.emplace("await", step("await", workflow::AwaitResult{result_probe, 2s, 0ms, 1ms}, {"done"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                if (mode == "nested") {
                    workflow::Definition child = std::move(root); child.id = "child";
                    child.steps.at("done").data = workflow::Return{"completed"};
                    program.definitions.emplace("child", std::move(child));
                    root = workflow::Definition{}; root.id = "root"; root.entry = "prepare";
                    root.steps.emplace("prepare", step("prepare", workflow::RegisteredOperation{"Prepare", nlohmann::json::object()}, {"call"}));
                    root.steps.emplace("call", step("call", workflow::Call{"child", {}}, {"done"}));
                    root.steps.emplace("done", step("done", workflow::Finish{}));
                }
                workflow::Definition boot; boot.id = "boot"; boot.entry = "boot";
                boot.steps.emplace("boot", step("boot", workflow::RegisteredOperation{"Boot", nlohmann::json::object()}, {"return"}));
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::EventRule event; event.id = "context-restart"; event.detect = scene;
                event.handler_definition = "boot"; event.on_device_restart = true; root.events.push_back(event);
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("boot", std::move(boot));
                runtime::FlowExecutor executor(program, ports, 3s, {1s, 1ms, 10ms, 2s});
                const auto result = drive(executor);
                require(result.state == runtime::TickState::ExternalBlocked && result.code == protected_reason,
                    "CRITICAL_PROTECTION_OUTCOME:" + mode + ":" + result.code);
                require(ports.submitted == 1 && ports.recoveries == 1 && ports.boots == 0 && ports.result_reads == 0,
                    "CRITICAL_PROTECTED_INPUT_REPLAYED_OR_FALSE_CONFIRMED:" + mode);
                require(executor.has_unresolved_input(), "CRITICAL_PENDING_DROPPED:" + mode);
                const auto pending = executor.progress_snapshot().at("pending_inputs");
                require(pending.size() == 1 && pending.at(0).at("action_epoch") == 1 &&
                        pending.at(0).at("attempts") == 1, "CRITICAL_PENDING_IDENTITY_CHANGED:" + mode);
                require(mode != "nested" || ports.prepares == 1, "CRITICAL_PARENT_PREPARE_REPLAYED");
                std::cout << "critical protected " << mode << ": preserved pending, no replay\n";
            }
            for (const bool transport_fault : {false, true}) {
                struct RestartPorts final : Ports {
                    int forced{}, recoveries{}, boots{}, prepares{};
                    bool restored{}, injected{}, transport{};
                    contracts::FrameEnvelope capture() override {
                        if (restored && !injected) {
                            injected = true;
                            throw contracts::ObservationUnavailable({transport ? contracts::ReadFaultKind::TransportUnavailable
                                : contracts::ReadFaultKind::ApplicationUnavailable, contracts::ReadFaultStage::Capture,
                                transport ? "ADB_OFFLINE" : "GAME_NOT_FOREGROUND", "test.first-restored-frame", {}, {}});
                        }
                        return Ports::capture();
                    }
                    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                                      const recognition::Request &request) override {
                        auto result = Ports::recognize(frame, request);
                        if (request.recognizer_id == "ready" && !restored)
                            result.outcome = contracts::RecognitionOutcome::NoHit;
                        return result;
                    }
                    contracts::ObservationRecovery recover_observation(bool restart = false) override {
                        ++recoveries;
                        if (restart) { ++forced; restored = true; }
                        return {{}, restart, true};
                    }
                    runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                        const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                        const std::string &) override {
                        if (binding == "Boot") ++boots;
                        if (binding == "Prepare") ++prepares;
                        return {runtime::OperationState::Done};
                    }
                } ports;
                ports.transport = transport_fault;
                auto ready = scene; ready.recognizer_id = "ready";
                workflow::FlowProgram program; program.revision = "critical-single-restart"; program.root_definition = "root";
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
                work.steps.at("dispatch").time_limit = 1s;
                work.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::Definition boot; boot.id = "boot"; boot.entry = "boot";
                boot.steps.emplace("boot", step("boot", workflow::RegisteredOperation{"Boot", nlohmann::json::object()}, {"return"}));
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::EventRule event; event.id = "context-restart"; event.detect = scene;
                event.handler_definition = "boot"; event.on_device_restart = true; root.events.push_back(event);
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("work", std::move(work));
                program.definitions.emplace("boot", std::move(boot));
                runtime::FlowExecutor executor(program, ports, 3s, {1s, 1ms, 10ms, 40ms});
                const auto result = drive(executor);
                require(result.state == runtime::TickState::Completed && ports.forced == 1 && ports.recoveries == 2 &&
                        ports.boots == 1 && ports.prepares == 1, "CRITICAL_RESTART_INTENT_NOT_CONSUMED:" + result.code);
                require(!executor.has_unresolved_input(), "CRITICAL_RESTART_CREATED_PENDING");
                const auto recovery = executor.progress_snapshot().at("observation_recovery");
                require(recovery.at("application_restarted_in_window").get<bool>() &&
                    !recovery.at("context_recovery").at("application_restarted").get<bool>(),
                    "CRITICAL_RESTART_AGGREGATE_LOST");
                std::cout << "critical restart post-fault=" << (transport_fault ? "transport" : "foreground")
                    << ": one forced restart, one Boot, one Prepare\n";
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--confirmed-result") {
            using J = nlohmann::json;
            struct ResultPorts final : Ports {
                std::uint64_t epoch{};
                int submissions{}, result_checks{};
                bool recorded{true};
                std::string mutation;
                contracts::FrameEnvelope capture() override {
                    auto frame = Ports::capture();
                    frame.identity.action_epoch = epoch;
                    frame.identity.raw_size = {900, 1600};
                    return frame;
                }
                contracts::Observation recognize(const contracts::FrameEnvelope &frame, const recognition::Request &request) override {
                    auto result = Ports::recognize(frame, request);
                    if (request.recognizer_id == "post") {
                        if (++result_checks > 1) result.outcome = contracts::RecognitionOutcome::NoHit;
                        if (recorded) result.evidence["confirmed_result"] = {{"classification", "navigation_no_route"}};
                    }
                    return result;
                }
                runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
                    const contracts::Observation &, contracts::Box, const std::string &) override {
                    ++submissions;
                    return {runtime::SubmissionState::Accepted, ++epoch, std::chrono::steady_clock::now(), {}};
                }
                runtime::OperationResult operate(const std::string &, const J &,
                    const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                    const std::string &) override {
                    if (mutation == "epoch") ++epoch;
                    return {runtime::OperationState::Done};
                }
            };
            for (const auto *scenario : {"valid", "wrong-class", "wrong-source", "epoch", "another-call", "no-record", "result-input"}) {
                recognition::Request image{"scene", "1", {0, 0, 900, 1600},
                    recognition::CustomParameters{"WvdVision", J::object()}};
                auto post = image; post.recognizer_id = "post";
                auto query = image;
                query.recognizer_id = "result";
                query.parameters = recognition::CustomParameters{"ConfirmedInputResult", {
                    {"mode", "confirmed_input_result"}, {"classification", std::string(scenario) == "wrong-class"
                        ? "harken_arrived" : "navigation_no_route"}, {"consume", true}}};
                if (std::string(scenario) == "wrong-source")
                    std::get<recognition::CustomParameters>(query.parameters).parameters["source_path"] = "foreign-call";
                workflow::FlowProgram program; program.revision = "recorded-result"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "call";
                root.steps.emplace("call", step("call", workflow::Call{"child"}, {"context"}));
                root.steps.emplace("context", std::string(scenario) == "another-call"
                    ? step("context", workflow::Call{"empty"}, {"choose"})
                    : step("context", workflow::RegisteredOperation{"context", J::object()}, {"choose"}));
                root.steps.emplace("choose", step("choose", workflow::Route{}, {"result", "missing"}));
                auto result_step = step("result", workflow::Observe{query}, {"again"}); result_step.guard = query;
                if (std::string(scenario) == "result-input") {
                    result_step.data = workflow::Input{query, image, {{"kind", "Click"}, {"x", 100}, {"y", 100}},
                        {1, 1, 898, 1598}, std::nullopt, false};
                    result_step.next = {"forbidden-await"};
                    root.steps.emplace("forbidden-await", step("forbidden-await", workflow::AwaitResult{post, 1s}, {"again"}));
                }
                root.steps.emplace("result", std::move(result_step));
                root.steps.emplace("again", step("again", workflow::Route{}, {"duplicate", "done"}));
                auto duplicate = step("duplicate", workflow::Observe{query}, {"bad"}); duplicate.guard = query;
                root.steps.emplace("duplicate", std::move(duplicate));
                root.steps.emplace("bad", step("bad", workflow::Fail{"RESULT_CONSUMED_TWICE"}));
                root.steps.emplace("missing", step("missing", workflow::Fail{"EXPECTED_RESULT_REJECTED"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::Definition child; child.id = "child"; child.entry = "click";
                workflow::Input input{image, image, {{"kind", "Click"}, {"x", 100}, {"y", 100}},
                    {1, 1, 898, 1598}, std::nullopt, false};
                child.steps.emplace("click", step("click", input, {"await"}));
                child.steps.emplace("await", step("await", workflow::AwaitResult{post, 1s}, {"wait"}));
                child.steps.emplace("wait", step("wait", workflow::Wait{5ms}, {"return"}));
                child.steps.emplace("return", step("return", workflow::Return{"completed"}));
                workflow::Definition empty; empty.id = "empty"; empty.entry = "return";
                empty.steps.emplace("return", step("return", workflow::Return{"completed"}));
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("child", std::move(child));
                program.definitions.emplace("empty", std::move(empty));
                ResultPorts ports;
                ports.recorded = std::string(scenario) != "no-record";
                ports.mutation = scenario;
                runtime::FlowExecutor executor(program, ports, 2s);
                runtime::TickResult result;
                do {
                    result = executor.tick();
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                } while (result.state == runtime::TickState::Progress || result.state == runtime::TickState::Waiting);
                if ((std::string(scenario) == "valid" ? result.state != runtime::TickState::Completed :
                        result.code != (std::string(scenario) == "result-input" ? "HISTORICAL_RESULT_CANNOT_AUTHORIZE_INPUT" :
                            "EXPECTED_RESULT_REJECTED")) || ports.submissions != 1 || executor.has_unresolved_input())
                    throw std::runtime_error("CONFIRMED_RESULT:" + std::string(scenario) + ":" + result.code);
                std::cout << "confirmed result " << scenario << " PASS; inputs=1; pending=0\n";
            }
            return 0;
        }
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
            for (bool reachable : {false, true}) for (bool moved : {false, true}) {
                recognition::Request stale{"stale", "1", {0, 0, 900, 1600},
                    recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
                auto battle = stale; battle.recognizer_id = "battle";
                workflow::FlowProgram program; program.revision = "timeout-scene"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "entry";
                auto entry = step("entry", workflow::Route{}, reachable
                    ? std::vector<std::string>{"stale", "battle"} : std::vector<std::string>{"stale"});
                entry.time_limit = 60ms; // The saved selection source owns the original waiting window.
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
                if ((moved && reachable) ? result.state != runtime::TickState::Completed :
                    (result.state != runtime::TickState::Failed || result.code != "FLOW_STAGE_TIMEOUT"))
                    throw std::runtime_error("TIMEOUT_RECLASSIFICATION:" + result.code);
                if (ports.operations || executor.has_unresolved_input())
                    throw std::runtime_error("TIMEOUT_RECLASSIFICATION_REPLAYED_INPUT");
            }
            std::cout << "timeout: declared origin recovers; unrelated known scene cannot jump phase; no inputs PASS\n";
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
        if (argc == 2 && std::string(argv[1]) == "--restart-during-boot") {
            const auto require = [](bool ok, const std::string &message) {
                if (!ok) throw std::runtime_error(message);
            };
            const auto drive = [](runtime::FlowExecutor &executor) {
                runtime::TickResult result;
                for (int i = 0; i < 500; ++i) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                    if (result.state == runtime::TickState::Waiting) std::this_thread::sleep_until(result.wake_at);
                }
                return result;
            };
            struct RestartBootPorts final : MenuRetryPorts {
                std::string scenario;
                int recoveries{}, prepares{}, resumes{}, parent_result_reads{}, cleanup_calls{};
                int required_recoveries() const { return scenario == "other-handler" ? 1 : 2; }
                contracts::FrameEnvelope capture() override {
                    if ((prepares && !recoveries && scenario != "other-handler") ||
                        (epoch && recoveries < required_recoveries()))
                        throw contracts::ObservationUnavailable({contracts::ReadFaultKind::ApplicationUnavailable,
                            contracts::ReadFaultStage::Capture, "GAME_NOT_FOREGROUND", "boot.capture", {}, {}});
                    return MenuRetryPorts::capture();
                }
                contracts::ObservationRecovery recover_observation(bool = false) override {
                    ++recoveries;
                    return {{}, true, true};
                }
                contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                                   const recognition::Request &request) override {
                    auto observed = MenuRetryPorts::recognize(frame, request);
                    if (request.recognizer_id == "work-ready" ||
                        (request.recognizer_id == "boot-ready" && recoveries < required_recoveries()) ||
                        (request.recognizer_id == "other-event" && (!prepares || epoch)))
                        observed.outcome = contracts::RecognitionOutcome::NoHit;
                    if (request.recognizer_id == "boot-result") {
                        ++parent_result_reads;
                        if (scenario == "unconfirmed" || recoveries < required_recoveries())
                            observed.outcome = contracts::RecognitionOutcome::NoHit;
                    }
                    return observed;
                }
                runtime::OperationResult operate(const std::string &binding, const nlohmann::json &,
                    const std::optional<contracts::FrameEnvelope> &, const std::optional<contracts::Observation> &,
                    const std::string &) override {
                    if (binding == "Prepare") ++prepares;
                    if (binding == "Resume") ++resumes;
                    return {runtime::OperationState::Done};
                }
                bool settle_observed_input() override { ++cleanup_calls; return false; }
                bool cancelled() const override {
                    return scenario == "unconfirmed" && parent_result_reads >= 2;
                }
            };
            const auto probe = [](const char *id) {
                return recognition::Request{id, "1", {0,0,900,1600},
                    recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            };
            for (const std::string scenario : {"confirmed", "unconfirmed", "cleanup-failed", "other-handler"}) {
                const auto ready = probe("boot-ready"), scene = probe("boot-scene"), result_probe = probe("boot-result");
                workflow::FlowProgram program; program.revision = "repeated-boot"; program.root_definition = "root";
                workflow::Definition root; root.id = "root"; root.entry = "prepare";
                root.steps.emplace("prepare", step("prepare", workflow::RegisteredOperation{"Prepare", nlohmann::json::object()}, {"work"}));
                root.steps.emplace("work", step("work", workflow::Observe{probe("work-ready")}, {"done"}));
                root.steps.emplace("resumed", step("resumed", workflow::RegisteredOperation{"Resume", nlohmann::json::object()}, {"done"}));
                root.steps.emplace("done", step("done", workflow::Finish{}));
                workflow::EventRule restart; restart.id = "context-restart"; restart.detect = ready;
                restart.handler_definition = "boot"; restart.on_device_restart = true;
                restart.resume = workflow::ResumeMode::Replan; restart.replan_step = "resumed"; restart.resume_guard = ready;
                root.events.push_back(restart);
                if (scenario == "other-handler") {
                    auto other = restart; other.id = "other"; other.on_device_restart = false;
                    other.category = workflow::EventClass::Overlay; other.detect = probe("other-event");
                    other.resume = workflow::ResumeMode::Reobserve;
                    root.events.push_back(std::move(other));
                }
                workflow::Definition boot; boot.id = "boot"; boot.entry = "dispatch";
                boot.steps.emplace("dispatch", step("dispatch", workflow::Route{}, {"ready", "title"}));
                auto observed = step("ready", workflow::Observe{ready}, {"return"}); observed.guard = ready;
                boot.steps.emplace("ready", std::move(observed));
                boot.steps.emplace("title", step("title", workflow::Input{scene, scene, {{"kind","Click"}}, {0,0,900,1600}}, {"await"}));
                boot.steps.emplace("await", step("await", workflow::AwaitResult{result_probe, 2s, 0ms, 1ms}, {"return"}));
                boot.steps.emplace("return", step("return", workflow::Return{"completed"}));
                program.definitions.emplace("root", std::move(root));
                program.definitions.emplace("boot", std::move(boot));
                RestartBootPorts ports; ports.scenario = scenario;
                if (scenario == "cleanup-failed") ports.mode = "delivery_unknown";
                runtime::FlowExecutor executor(program, ports, 3s, {1s, 1ms, 5ms, 2s});
                const auto result = drive(executor);
                require(ports.prepares == 1 && ports.epoch == 1 && ports.recoveries == ports.required_recoveries(),
                    "REPEATED_BOOT_REPLAYED:" + scenario + ":" + result.code);
                if (scenario == "confirmed") {
                    require(result.state == runtime::TickState::Completed && ports.resumes == 1 && !executor.has_unresolved_input(),
                        "REPEATED_BOOT_FAILED:" + result.code);
                } else {
                    require(ports.resumes == 0 && executor.has_unresolved_input(), "REPEATED_BOOT_DROPPED_PENDING:" + scenario);
                    if (scenario == "unconfirmed")
                        require(result.state == runtime::TickState::Cancelled && ports.parent_result_reads >= 2,
                            "REPEATED_BOOT_FALSE_CONFIRMATION:" + result.code);
                    else require(result.state == runtime::TickState::ExternalBlocked &&
                        result.code == (scenario == "cleanup-failed" ? "OBSERVED_RESULT_INPUT_CLEANUP_UNCONFIRMED" :
                            "EVENT_REPLAN_CROSSES_ACTIVE_HANDLER"), "REPEATED_BOOT_BOUNDARY:" + scenario + ":" + result.code);
                }
                std::cout << "repeated boot " << scenario << ": " << result.code << '\n';
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
            for (const auto *mode : {"black", "progress", "stop", "expired-call"}) {
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
                if (std::string(mode) == "expired-call") {
                    work.cumulative_budget = 5ms; // Expires before the 30ms exception restart threshold.
                    boot.steps.emplace("settle", step("settle", workflow::Wait{20ms}, {"return"}));
                    boot.steps.at("boot").next = {"settle"};
                    auto &restart = root.events.back();
                    restart.resume = workflow::ResumeMode::Replan;
                    restart.replan_step = "resumed"; restart.resume_guard = ready;
                    root.steps.emplace("resumed", step("resumed", workflow::Route{}, {"done"}));
                }
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
                const bool black = std::string(mode) == "black" || std::string(mode) == "expired-call";
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
