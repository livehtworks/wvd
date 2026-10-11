#pragma once

#include "runtime/flow_executor.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

// Drive the actual read-recovery/on_device_restart path. Only device frames,
// recognition outcomes and backend receipts are controlled; no private stack
// or synthetic recovery JSON is injected into FlowExecutor.
namespace ancestor_unknown_contract {
using namespace wvd;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using J = nlohmann::json;

inline recognition::Request request(const char *id) {
    return {id, "1", {0, 0, 900, 1600},
        recognition::CustomParameters{"AncestorUnknownFixture", J::object()}};
}
inline workflow::Step make_step(const char *id, workflow::StepData data,
                                std::vector<std::string> next = {}) {
    workflow::Step result;
    result.id = result.source_path = id;
    result.data = std::move(data); result.next = std::move(next);
    result.time_limit = 1s;
    return result;
}
struct Ports final : runtime::FlowPorts {
    bool unknown{}, recovered{};
    std::uint64_t frames{}, epoch{};
    unsigned root_sends{}, boot_sends{}, prepares{}, recoveries{}, cleanups{}, effects{};
    explicit Ports(bool value) : unknown(value) {}
    bool prepare_input() override { ++prepares; return false; }
    contracts::FrameEnvelope capture() override {
        if (root_sends && !recovered)
            throw contracts::ObservationUnavailable({contracts::ReadFaultKind::TransportUnavailable,
                contracts::ReadFaultStage::Capture, "DEVICE_INSTANCE_RESTART_REQUIRED",
                "ancestor.fixture.capture", {}, {}});
        contracts::FrameEnvelope frame;
        auto &id = frame.identity;
        id.device_id = "ancestor-offline"; id.game_id = "wvd";
        id.pack_revision = "ancestor-contract"; id.viewport_id = "900x1600";
        id.instance_id = "2"; id.instance_created_identity = "fixture-created";
        id.generation = 1; id.connection_generation = recovered ? 2 : 1;
        id.frame_id = ++frames; id.action_epoch = epoch;
        id.raw_size = id.recognition_size = {900, 1600};
        id.captured_at = id.capture_finished_at = Clock::now();
        return frame;
    }
    contracts::ObservationRecovery recover_observation(bool = false) override {
        ++recoveries; recovered = true;
        return {contracts::ObservationReconnect{"ancestor-offline", "2", "fixture-created", 1, 2},
            true, true, true,
            contracts::InstanceExitProof{"ancestor-offline", "2", "fixture-created", 1, 2,
                Clock::now(), 1}};
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                      const recognition::Request &req) override {
        contracts::Observation value;
        value.basis = frame.identity;
        value.outcome = contracts::RecognitionOutcome::Hit;
        value.action_eligible = req.recognizer_id == "root.target" || req.recognizer_id == "boot.target";
        value.center = contracts::Point{450, 800}; value.box = contracts::Box{440, 790, 20, 20};
        if (req.recognizer_id == "root.result" ||
            ((req.recognizer_id == "boot.ready" || req.recognizer_id == "boot.result") && !boot_sends))
            value.outcome = contracts::RecognitionOutcome::NoHit;
        return value;
    }
    runtime::Submission submit(const contracts::Command &, const contracts::Observation &,
        const contracts::Observation &, contracts::Box, const std::string &source) override {
        if (source == "root.input") ++root_sends;
        else if (source == "boot.input") ++boot_sends;
        else throw std::runtime_error("ANCESTOR_UNEXPECTED_INPUT:" + source);
        return {unknown && source == "root.input" ? runtime::SubmissionState::Unresolved :
                runtime::SubmissionState::Accepted, ++epoch, Clock::now(), {}};
    }
    runtime::OperationResult operate(const std::string &, const J &,
        const std::optional<contracts::FrameEnvelope> &,
        const std::optional<contracts::Observation> &, const std::string &) override {
        ++effects; return {runtime::OperationState::Done};
    }
    bool settle_observed_input() override { ++cleanups; return false; }
    bool cancelled() const override { return false; }
};

inline workflow::FlowProgram program() {
    workflow::FlowProgram p; p.revision = "ancestor-contract"; p.root_definition = "root";
    workflow::Definition root; root.id = "root"; root.entry = "root.input";
    workflow::Input initial{request("root.scene"), request("root.target"),
        {{"kind", "Click"}}, {0, 0, 900, 1600}};
    initial.retry = workflow::InputRetry{request("root.scene"), 1s, 2};
    // This ordinary menu may be reclassified only for an Accepted receipt.
    // No instance_exit_discardable or interruption bypass is granted to Unknown.
    root.steps.emplace("root.input", make_step("root.input", initial, {"root.await"}));
    root.steps.emplace("root.await", make_step("root.await",
        workflow::AwaitResult{request("root.result"), 1s, 0ms, 1ms}, {"root.done"}));
    root.steps.emplace("root.done", make_step("root.done", workflow::Finish{}));
    workflow::EventRule restart;
    restart.id = "context-restart"; restart.category = workflow::EventClass::Exception;
    restart.detect = request("boot.ready"); restart.handler_definition = "boot";
    restart.on_device_restart = true; restart.resume = workflow::ResumeMode::Replan;
    restart.replan_step = "root.done"; restart.resume_guard = request("boot.ready");
    root.events.push_back(restart);
    workflow::Definition boot; boot.id = "boot"; boot.entry = "boot.dispatch";
    boot.steps.emplace("boot.dispatch", make_step("boot.dispatch", workflow::Route{}, {"boot.ready", "boot.input"}));
    auto ready = make_step("boot.ready", workflow::Observe{request("boot.ready")}, {"boot.return"});
    ready.guard = request("boot.ready"); boot.steps.emplace("boot.ready", std::move(ready));
    boot.steps.emplace("boot.input", make_step("boot.input",
        workflow::Input{request("boot.scene"), request("boot.target"), {{"kind", "Click"}},
            {0, 0, 900, 1600}}, {"boot.await"}));
    boot.steps.emplace("boot.await", make_step("boot.await",
        workflow::AwaitResult{request("boot.result"), 1s, 0ms, 1ms}, {"boot.return"}));
    boot.steps.emplace("boot.return", make_step("boot.return", workflow::Return{"completed"}));
    p.definitions.emplace(root.id, std::move(root));
    p.definitions.emplace(boot.id, std::move(boot));
    return p;
}

inline int run() {
    for (const bool unknown : {true, false}) {
        auto p = program(); Ports ports(unknown);
        runtime::FlowExecutor executor(p, ports, 2s, {1s, 1ms, 2ms, 1s});
        runtime::TickResult last{runtime::TickState::Progress}; J original;
        do {
            last = executor.tick();
            if (ports.root_sends && original.is_null())
                original = executor.progress_snapshot().at("pending_inputs");
            if (last.state == runtime::TickState::Waiting) std::this_thread::sleep_until(last.wake_at);
        } while (last.state == runtime::TickState::Progress || last.state == runtime::TickState::Waiting);
        const auto snapshot = executor.progress_snapshot();
        const auto proof = snapshot.at("observation_recovery").at("instance_exit_proof");
        bool ok = ports.root_sends == 1 && ports.recoveries == 1 && ports.effects == 0 &&
            ports.cleanups == 0 && contracts::exit_covers_receipts(proof, original);
        if (unknown) ok &= ports.boot_sends == 0 && ports.prepares == 1 &&
            last.state == runtime::TickState::ExternalBlocked &&
            last.code == "INPUT_BLOCKED_BY_UNKNOWN_DELIVERY" &&
            executor.has_unresolved_input() && snapshot.at("pending_inputs") == original;
        else ok &= ports.boot_sends == 1 && ports.prepares == 2 &&
            last.state == runtime::TickState::Completed && !executor.has_unresolved_input();
        std::cout << J{{"case", unknown ? "unknown_ancestor_reconnect_input" : "accepted_menu_reconnect_input"},
            {"pass", ok}, {"state", static_cast<int>(last.state)}, {"code", last.code},
            {"root_sends", ports.root_sends}, {"boot_sends", ports.boot_sends},
            {"prepare_calls", ports.prepares}, {"recoveries", ports.recoveries},
            {"cleanup_calls", ports.cleanups}, {"effects", ports.effects},
            {"exit_proof_covers_original", contracts::exit_covers_receipts(proof, original)},
            {"original_pending_preserved", snapshot.at("pending_inputs") == original}}.dump() << '\n';
        if (!ok) throw std::runtime_error("ANCESTOR_UNKNOWN_RECONNECT_CONTRACT");
    }
    return 0;
}
} // namespace ancestor_unknown_contract
