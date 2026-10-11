#pragma once

#include "runtime/flow_executor.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

// Actual FlowExecutor; capture/recognition/transport are controlled ports.
// No Service, emulator, real pixels, business recovery or input cleanup is faked.
namespace manual_stop_contract {
using namespace wvd;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using J = nlohmann::json;

inline recognition::Request request(const std::string &id) {
    return {id, "1", {0, 0, 900, 1600},
        recognition::CustomParameters{"ManualStopFixture", J::object()}};
}
inline workflow::Step make_step(std::string id, workflow::StepData data,
                                std::vector<std::string> next = {}) {
    workflow::Step value;
    value.id = id; value.source_path = J::array({id}).dump();
    value.data = std::move(data); value.next = std::move(next);
    return value;
}
inline bool terminal(runtime::TickState state) {
    return state != runtime::TickState::Progress && state != runtime::TickState::Waiting;
}
struct Ports final : runtime::FlowPorts {
    std::string mode;
    std::uint64_t epoch{}, captures{};
    unsigned sends{}, action_probes_after_send{}, action_probes_while_unknown{}, stop_probes_after_send{}, effects{}, cleanups{}, recoveries{};
    bool handled{}, scene_delayed{};
    explicit Ports(std::string value) : mode(std::move(value)) {}
    contracts::FrameEnvelope capture() override {
        contracts::FrameEnvelope frame;
        auto &id = frame.identity;
        id.device_id = "manual-stop-memory-device"; id.game_id = "wvd";
        id.pack_revision = "manual-stop-contract"; id.viewport_id = "900x1600";
        id.instance_id = "instance"; id.instance_created_identity = "created";
        id.generation = id.connection_generation = 1;
        id.frame_id = ++captures; id.action_epoch = epoch;
        id.raw_size = id.recognition_size = {900, 1600};
        id.captured_at = id.capture_finished_at = Clock::now();
        return frame;
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &req) override {
        contracts::Observation value;
        value.basis = frame.identity; value.outcome = contracts::RecognitionOutcome::Hit;
        value.action_eligible = req.recognizer_id == "target";
        value.box = contracts::Box{440, 790, 20, 20}; value.center = contracts::Point{450, 800};
        if (req.recognizer_id == "scene" && (mode == "timer_already_armed" || mode.starts_with("deadline_")) && !scene_delayed) {
            scene_delayed = true; value.outcome = contracts::RecognitionOutcome::NoHit;
        } else if (req.recognizer_id == "result") {
            const bool complete = mode == "result_confirmed" || mode == "cleanup_unconfirmed" || handled;
            value.outcome = complete ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
        } else if (req.recognizer_id == "event.action") {
            if (sends) ++action_probes_after_send;
            // A confirmed original result releases the Unknown restriction.
            // Normal event scans after successful cleanup are legal.
            if (sends && !cleanups && mode != "accepted_manual" && mode != "accepted_handler")
                ++action_probes_while_unknown;
            value.outcome = sends && !handled && mode != "result_confirmed"
                ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
        } else if (req.recognizer_id == "event.manual") {
            if (sends) ++stop_probes_after_send;
            if (!sends || mode == "nohit" || mode == "timer_already_armed" || mode.starts_with("deadline_") ||
                mode == "result_confirmed" || mode == "cleanup_unconfirmed")
                value.outcome = contracts::RecognitionOutcome::NoHit;
            else if (mode == "recognition_error") {
                value.outcome = contracts::RecognitionOutcome::Error;
                value.error_code = "MANUAL_STOP_RECOGNITION_ERROR";
            } else if (mode == "stale_evidence") ++value.basis.connection_generation;
        }
        return value;
    }
    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
        const contracts::Observation &, contracts::Box, const std::string &) override {
        ++sends;
        const bool accepted = mode == "accepted_manual" || mode == "accepted_handler";
        return {accepted ? runtime::SubmissionState::Accepted : runtime::SubmissionState::Unresolved,
            ++epoch, Clock::now(), {}};
    }
    runtime::OperationResult operate(const std::string &, const J &,
        const std::optional<contracts::FrameEnvelope> &,
        const std::optional<contracts::Observation> &, const std::string &) override {
        ++effects; handled = true; return {runtime::OperationState::Done};
    }
    bool settle_observed_input() override { ++cleanups; return mode != "cleanup_unconfirmed"; }
    contracts::ObservationRecovery recover_observation(bool = false) override { ++recoveries; return {}; }
    bool cancelled() const override { return false; }
};

inline workflow::FlowProgram program(const std::string &mode) {
    workflow::FlowProgram p; p.revision = "manual-stop-contract"; p.root_definition = "root";
    workflow::Definition root; root.id = "root"; root.entry = "input";
    root.checks = workflow::Definition::CheckPolicy{"fixture", {}, 0ms, 1ms};
    workflow::Input input{request("scene"), request("target"), {{"kind", "Click"}}, {0, 0, 900, 1600}};
    input.retry = workflow::InputRetry{request("retry.ready"), 1s, 0};
    root.steps.emplace("input", make_step("input", input, {"await"}));
    root.steps.emplace("await", make_step("await", workflow::AwaitResult{request("result"), mode == "action_only" ? 2s : 600ms,
        mode == "initial_delay" ? 300ms : 0ms, 1ms}, {"finish"}));
    root.steps.emplace("finish", make_step("finish", workflow::Finish{}));
    // Let the production 50ms re-observation and Windows scheduling reach the
    // first submission before testing a local deadline with Unknown pending.
    if (mode == "deadline_await") std::get<workflow::AwaitResult>(root.steps.at("await").data).budget = 300ms;
    if (mode == "deadline_stage") root.steps.at("await").time_limit = 300ms;
    if (mode == "deadline_invocation") root.cumulative_budget = 300ms;
    if (mode == "deadline_phase") {
        root.entry = "phase";
        root.steps.emplace("phase", make_step("phase", workflow::RegisteredOperation{"BeginObservationPhase",
            {{"phase", "fixture"}, {"budget_ms", 300}}}, {"input"}));
    }
    workflow::EventRule action;
    action.id = "action"; action.category = workflow::EventClass::Overlay; action.priority = 900;
    action.detect = request("event.action"); action.handler_definition = "handler";
    if (mode != "accepted_manual") root.events.push_back(action);
    workflow::EventRule manual;
    manual.id = "manual"; manual.priority = 100;
    manual.detect = request("event.manual");
    manual.disposition = workflow::EventDisposition::ExternalBlocked;
    manual.reason = "party.multiple_deaths_manual_required";
    if (mode == "exception") manual.category = workflow::EventClass::Exception;
    if (mode == "special") manual.category = workflow::EventClass::Special;
    if (mode == "encounter") manual.category = workflow::EventClass::Encounter;
    if (mode == "handler_metadata") manual.handler_definition = "handler";
    if (mode == "replan_metadata") {
        manual.resume = workflow::ResumeMode::Replan;
        manual.replan_step = "finish"; manual.resume_guard = request("scene");
    }
    if (mode != "action_only" && mode != "accepted_handler") root.events.push_back(manual);
    // A real restart declaration enables the production timeout branch, so the
    // no-automatic-restart assertion does not pass merely because it is absent.
    auto restart = action;
    restart.id = "restart"; restart.on_device_restart = true;
    restart.resume = workflow::ResumeMode::Replan;
    restart.replan_step = "finish"; restart.resume_guard = request("scene");
    root.events.push_back(restart);
    workflow::Definition handler; handler.id = "handler"; handler.entry = "effect";
    handler.checks = workflow::Definition::CheckPolicy{"handler", {}, 0ms, 1ms};
    handler.steps.emplace("effect", make_step("effect", workflow::RegisteredOperation{"effect", J::object()}, {"return"}));
    handler.steps.emplace("return", make_step("return", workflow::Return{"completed", {}, {}}));
    if (mode == "scope_filtered") {
        workflow::Definition child; child.id = "child"; child.entry = "input";
        child.checks = workflow::Definition::CheckPolicy{"child", {}, 0ms, 1ms};
        child.steps = root.steps;
        child.steps.at("finish").data = workflow::Return{"completed", {}, {}};
        root.steps.clear(); root.entry = "call";
        root.steps.emplace("call", make_step("call", workflow::Call{"child", {}}, {"finish"}));
        root.steps.emplace("finish", make_step("finish", workflow::Finish{}));
        p.definitions.emplace(child.id, std::move(child));
    }
    p.definitions.emplace(root.id, std::move(root));
    p.definitions.emplace(handler.id, std::move(handler));
    return p;
}

inline int run() {
    unsigned failures{};
    for (const auto *name : {"overlay", "exception", "special", "encounter", "initial_delay",
            "nohit", "action_only", "scope_filtered", "handler_metadata", "replan_metadata",
            "recognition_error", "stale_evidence", "result_confirmed", "cleanup_unconfirmed",
            "timer_already_armed", "deadline_await", "deadline_stage", "deadline_invocation", "deadline_phase",
            "accepted_manual", "accepted_handler"}) {
        const std::string mode = name;
        auto p = program(mode); Ports ports(mode);
        // The first Unknown submission must precede this clock. Otherwise the
        // executor correctly reclassifies the recovered scene before any send.
        contracts::ObservationRecoveryPolicy policy; policy.exception_timeout = 100ms;
        if (mode == "handler_metadata" || mode == "replan_metadata") {
            std::string error;
            try { runtime::FlowExecutor invalid(p, ports, 3s, policy); }
            catch (const std::exception &failure) { error = failure.what(); }
            const std::string field = mode == "handler_metadata" ? "handler_definition" : "resume";
            const bool ok = error.starts_with("FLOW_EVENT_STOP_INVALID:root:") &&
                error.ends_with(":manual:" + field) && ports.captures == 0 && ports.sends == 0 &&
                ports.effects == 0 && ports.recoveries == 0 && ports.cleanups == 0;
            failures += !ok;
            std::cout << J{{"case", mode}, {"pass", ok}, {"validation_error", error},
                {"captures", ports.captures}, {"sends", ports.sends}, {"effects", ports.effects},
                {"recoveries", ports.recoveries}, {"cleanup_calls", ports.cleanups}}.dump() << '\n';
            continue;
        }
        runtime::FlowExecutor executor(p, ports, 3s, policy);
        runtime::TickResult last{runtime::TickState::Progress};
        J original;
        J initial_ticks = J::array();
        bool armed_at_submission{};
        const auto window = mode == "action_only" ? 1150ms : mode.starts_with("deadline_") ? 700ms
            : mode == "accepted_handler" ? 1000ms : 200ms;
        const auto until = Clock::now() + window;
        while (Clock::now() < until && !terminal(last.state)) {
            last = executor.tick();
            if (initial_ticks.size() < 4 && (mode == "timer_already_armed" || mode.starts_with("deadline_")))
                initial_ticks.push_back({{"node",executor.current_step_id()}, {"sends",ports.sends},
                    {"exception",executor.progress_snapshot().at("continuous_exception")},
                    {"diagnostic",executor.progress_snapshot().at("last_diagnostic")}});
            if (ports.sends && original.is_null()) {
                const auto progress = executor.progress_snapshot();
                original = progress.at("pending_inputs");
                armed_at_submission = progress.at("continuous_exception").at("active");
            }
            if (ports.sends || !(mode == "timer_already_armed" || mode.starts_with("deadline_")))
                std::this_thread::sleep_for(1ms);
        }
        const auto snapshot = executor.progress_snapshot();
        const bool accepted = mode == "accepted_manual" || mode == "accepted_handler";
        const bool completed = mode == "result_confirmed" || mode == "accepted_handler";
        const bool blocked = mode == "overlay" || mode == "exception" || mode == "special" ||
            mode == "encounter" || mode == "initial_delay" || mode == "accepted_manual";
        const bool recognition_failed = mode == "recognition_error" || mode == "stale_evidence";
        bool ok = ports.sends == 1 && ports.recoveries == 0 &&
            (mode == "accepted_handler" ? ports.effects == 1 : ports.effects == 0) &&
            ports.action_probes_while_unknown == 0;
        if (completed) ok &= last.state == runtime::TickState::Completed && !executor.has_unresolved_input();
        else {
            ok &= executor.has_unresolved_input() && snapshot.at("pending_inputs") == original;
            if (blocked) ok &= last.state == runtime::TickState::ExternalBlocked &&
                last.code == "party.multiple_deaths_manual_required";
            else if (recognition_failed) ok &= last.state == runtime::TickState::Failed &&
                last.code == (mode == "stale_evidence" ? "EVENT_EVIDENCE_STALE" : "MANUAL_STOP_RECOGNITION_ERROR");
            else if (mode == "cleanup_unconfirmed") ok &= last.state == runtime::TickState::ExternalBlocked &&
                last.code == "OBSERVED_RESULT_INPUT_CLEANUP_UNCONFIRMED";
            else if (mode == "deadline_await" || mode == "deadline_stage") ok &=
                last.state == runtime::TickState::ExternalBlocked &&
                last.code == (mode == "deadline_await" ? "INPUT_RESULT_UNCONFIRMED:AWAIT_RESULT_TIMEOUT"
                    : "INPUT_RESULT_UNCONFIRMED:FLOW_STAGE_TIMEOUT");
            else if (mode == "deadline_invocation" || mode == "deadline_phase") ok &=
                last.state == runtime::TickState::Failed &&
                last.code == (mode == "deadline_invocation" ? "FLOW_INVOCATION_TIMEOUT:root"
                    : "OBSERVATION_PHASE_TIMEOUT:fixture");
            else ok &= !terminal(last.state);
        }
        if (mode == "result_confirmed" || mode == "cleanup_unconfirmed") ok &= ports.cleanups == 1;
        else ok &= ports.cleanups == 0;
        if (!accepted && mode != "timer_already_armed" && !mode.starts_with("deadline_"))
            ok &= snapshot.at("continuous_exception").at("active") == false;
        if (mode == "timer_already_armed" || mode.starts_with("deadline_"))
            ok &= armed_at_submission && snapshot.at("continuous_exception").at("active") == true;
        if (mode == "scope_filtered")
            ok &= ports.stop_probes_after_send == 0;
        failures += !ok;
        std::cout << J{{"case", mode}, {"pass", ok}, {"state", static_cast<int>(last.state)},
            {"code", last.code}, {"sends", ports.sends}, {"effects", ports.effects},
            {"recoveries", ports.recoveries}, {"cleanup_calls", ports.cleanups},
            {"scene_delayed",ports.scene_delayed}, {"continuous_exception",snapshot.at("continuous_exception")},
            {"initial_ticks",initial_ticks},
            {"armed_at_submission",armed_at_submission},
            {"action_probes_after_send", ports.action_probes_after_send},
            {"action_probes_while_unknown", ports.action_probes_while_unknown},
            {"manual_probes_after_send", ports.stop_probes_after_send},
            {"pending_preserved", !original.is_null() && snapshot.at("pending_inputs") == original}}.dump() << '\n';
    }
    if (failures) throw std::runtime_error("UNKNOWN_INPUT_MANUAL_STOP_CASES_FAILED:" + std::to_string(failures));
    std::cout << "PASS 21 unknown-input/manual-stop, deadline and accepted-input controls\n";
    return 0;
}

inline int scope_copy(std::atomic<bool> &watch, std::atomic<std::uint64_t> &large_allocations) {
    workflow::FlowProgram p; p.revision = "scope-copy-contract"; p.root_definition = "root";
    workflow::Definition root; root.id = "root"; root.entry = "wait";
    root.steps.emplace("wait", make_step("wait", workflow::Wait{2s}, {"done"}));
    root.steps.emplace("done", make_step("done", workflow::Finish{}));
    workflow::EventRule event;
    event.id = "large-readonly-rule"; event.category = workflow::EventClass::Exception;
    event.disposition = workflow::EventDisposition::ExternalBlocked; event.reason = "manual";
    event.detect = request("unused");
    std::get<recognition::CustomParameters>(event.detect.parameters).parameters = {{"padding", std::string(65536, 'x')}};
    root.events.push_back(std::move(event)); p.definitions.emplace(root.id, std::move(root));
    Ports ports("scope_copy"); runtime::FlowExecutor executor(p, ports, 3s);
    (void)executor.tick(); // Populate the existing scope cache outside the measured interval.
    large_allocations = 0; watch = true;
    for (int i = 0; i < 8; ++i) (void)executor.tick();
    watch = false;
    const auto measured = large_allocations.load();
    std::cout << J{{"case", "unchanged_scope_wait_ticks"}, {"ticks", 8},
        {"allocations_at_least_16KiB", measured}, {"captures", ports.captures},
        {"pass", measured == 0 && ports.captures == 0}}.dump() << '\n';
    if (measured || ports.captures) throw std::runtime_error("UNCHANGED_EVENT_SCOPE_COPIED");
    return 0;
}
} // namespace manual_stop_contract
