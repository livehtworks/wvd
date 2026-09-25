#include "runtime/flow_executor.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

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
        if (request.recognizer_id == "event.network" && frame.identity.frame_id != 3)
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

int main() {
    try {
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
                throw std::runtime_error("EVENT_SCOPE_MISMATCH:" + actual + ":" + expected);
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
        std::cout << "flow reobservation, event scope and exit budget passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
