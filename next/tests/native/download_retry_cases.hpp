#pragma once

#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/vision/boot_probes.hpp"
#include "runtime/flow_executor.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

// Offline contract proof: real public document/compiler/executor. Frames and
// leaf outcomes are controlled memory objects; no Service/device/game is used.
namespace download_contract_test {
using J = nlohmann::json;
using C = wvd::games::tasks::PipelineCompiler;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

inline void require(bool value, const char *code) {
    if (!value) throw std::runtime_error(code);
}
inline J read(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("DOWNLOAD_TEST_INPUT_MISSING:" + path.string());
    return J::parse(input);
}
inline void write(const std::filesystem::path &path, const J &value) {
    std::ofstream output(path); output << value.dump(2) << '\n';
    require(bool(output), "DOWNLOAD_TEST_OUTPUT_FAILED");
}

struct Surface { bool download{true}, network{}, pause{}, wheel{}; };
struct Ports final : wvd::runtime::FlowPorts {
    std::string locale, scenario;
    Surface surface;
    J wheel;
    std::map<std::uint64_t, Surface> frames;
    std::uint64_t counter{}, epoch{};
    Clock::time_point began{Clock::now()};
    std::vector<std::int64_t> submitted_ms;
    J inputs = J::array(), results = J::array();
    bool cancel{};
    Ports(std::string lang, std::string name, J ready)
        : locale(std::move(lang)), scenario(std::move(name)), wheel(std::move(ready)) {}
    std::int64_t elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count();
    }
    wvd::contracts::FrameEnvelope capture() override {
        wvd::contracts::FrameEnvelope frame;
        auto &id = frame.identity;
        id.device_id = "download-contract-memory-device"; id.game_id = "wvd";
        id.pack_revision = "download-contract"; id.viewport_id = "900x1600";
        id.instance_id = "download-contract-instance";
        id.instance_created_identity = "download-contract-created";
        id.generation = id.connection_generation = 1;
        id.action_epoch = epoch; id.frame_id = ++counter;
        id.raw_size = id.recognition_size = {900, 1600};
        id.captured_at = id.capture_finished_at = Clock::now();
        frames[id.frame_id] = surface;
        return frame;
    }
    bool evaluate(const J &p, const Surface &s) const {
        if (p.contains("locale_only") && p.at("locale_only").get<std::string>() != locale) return false;
        // Exact production page probes remain atomic controlled evidence;
        // input_clear itself expands the production blocker recipe.
        if (p == wheel) return s.wheel;
        if (p == wvd::games::vision::network_retry_prompt()) return s.network;
        const auto mode = p.value("mode", "");
        if (mode == "all" || mode == "any" || mode == "not") {
            bool all = true, any = false;
            for (const auto &child : p.at("conditions")) {
                const auto hit = evaluate(child, s); all &= hit; any |= hit;
            }
            return mode == "all" ? all : mode == "any" ? any : !any;
        }
        if (mode == "pause") return s.pause;
        if (mode == "input_clear") {
            for (const auto &blocker : wvd::games::vision::input_blockers(p.value("phase", ""), locale, true))
                if (evaluate(blocker, s)) return false;
            return true;
        }
        if (mode == "template") {
            const auto image = p.value("image", "");
            // request() flattens adjacent any groups, so the original network
            // composite need not survive as one JSON subtree in retry.ready.
            if (s.network && (locale == "en" ? image == "retry" :
                image == "network_error_zh_hant" || image == "network_retry_zh_hant")) return true;
            return s.download && image == (locale == "en" ? "startdownload" : "startdownload_zh_hant");
        }
        return false;
    }
    wvd::contracts::Observation recognize(const wvd::contracts::FrameEnvelope &frame,
                                           const wvd::recognition::Request &request) override {
        wvd::contracts::Observation result; result.basis = frame.identity;
        const auto &p = std::get<wvd::recognition::CustomParameters>(request.parameters).parameters;
        result.outcome = evaluate(p, frames.at(frame.identity.frame_id))
            ? wvd::contracts::RecognitionOutcome::Hit : wvd::contracts::RecognitionOutcome::NoHit;
        result.action_eligible = true;
        result.box = wvd::contracts::Box{360, 1190, 180, 70};
        result.center = wvd::contracts::Point{450, 1225};
        return result;
    }
    wvd::runtime::Submission submit(const wvd::contracts::Command &command,
        const wvd::contracts::Observation &scene, const wvd::contracts::Observation &target,
        wvd::contracts::Box, const std::string &path) override {
        require(path.find("Download") != std::string::npos, "DOWNLOAD_TEST_UNEXPECTED_INPUT_PATH");
        require(scene.basis.frame_id == target.basis.frame_id, "DOWNLOAD_TEST_MIXED_FRAME");
        require(command.x == 450 && command.y == 1225, "DOWNLOAD_TEST_TARGET_MOVED");
        const auto ms = elapsed(); submitted_ms.push_back(ms); ++epoch;
        inputs.push_back({{"ms", ms}, {"path", path}, {"frame_id", scene.basis.frame_id},
            {"action_epoch", epoch}, {"x", command.x}, {"y", command.y}});
        if (inputs.size() == 1) {
            if (scenario == "network_overlay") surface.network = true;
            if (scenario == "pause_overlay") surface.pause = true;
            if (scenario == "target_disappeared") { surface.download = false; surface.wheel = true; }
        }
        if (scenario == "lost_first" && inputs.size() == 2) {
            surface.download = false; surface.wheel = true;
        }
        return {scenario == "delivery_unknown" ? wvd::runtime::SubmissionState::Unresolved
            : wvd::runtime::SubmissionState::Accepted, epoch, Clock::now(), {}};
    }
    wvd::runtime::OperationResult operate(const std::string &, const J &,
        const std::optional<wvd::contracts::FrameEnvelope> &,
        const std::optional<wvd::contracts::Observation> &, const std::string &) override {
        return {wvd::runtime::OperationState::Done};
    }
    void input_result(const J &value) noexcept override { try { results.push_back(value); } catch (...) {} }
    bool cancelled() const override { return cancel; }
};

inline bool terminal(wvd::runtime::TickState state) {
    return state != wvd::runtime::TickState::Waiting && state != wvd::runtime::TickState::Progress;
}
inline const char *state_name(wvd::runtime::TickState state) {
    switch (state) {
    case wvd::runtime::TickState::Waiting: return "Waiting";
    case wvd::runtime::TickState::Progress: return "Progress";
    case wvd::runtime::TickState::Completed: return "Completed";
    case wvd::runtime::TickState::Failed: return "Failed";
    case wvd::runtime::TickState::BusinessFailed: return "BusinessFailed";
    case wvd::runtime::TickState::ExternalBlocked: return "ExternalBlocked";
    case wvd::runtime::TickState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
struct Case {
    std::string locale, scenario;
    wvd::workflow::FlowProgram program;
    Ports ports;
    wvd::runtime::FlowExecutor executor;
    wvd::runtime::TickResult last{wvd::runtime::TickState::Progress};
    Case(std::string lang, std::string name, const wvd::games::tasks::CompiledWorkflow &graph, J ready)
        : locale(std::move(lang)), scenario(std::move(name)),
          program(wvd::games::tasks::compile_native_program(graph, J::object(), "download-contract")),
          ports(locale, scenario, std::move(ready)), executor(program, ports, 30s) {}
};

inline int run(const std::filesystem::path &next_root, const std::filesystem::path &output,
               bool expect_fixed) {
    require(!std::filesystem::exists(output), "DOWNLOAD_TEST_NEW_OUTPUT_REQUIRED");
    std::filesystem::create_directories(output);
    const auto assets = read(next_root / "resources/authoring/semantic-assets.json");
    J docs = J::object();
    for (const auto &doc : read(next_root / "resources/authoring/public-flows.json"))
        docs[doc.at("flow").at("id").get<std::string>()] = doc;
    const wvd::games::tasks::PublicFlowLibrary library(docs, assets);
    J snapshots = J::array(), conditions = J::array();
    std::vector<std::unique_ptr<Case>> cases;
    for (const auto *locale : {"en", "zh-Hant"}) {
        auto graph = library.compile_step("wheel-open", J::object(), locale);
        const auto before_locale = graph.nodes.at("Author_Download").at("operation_args");
        wvd::games::tasks::localize_task_assets(graph, assets, locale);
        const auto &action = graph.nodes.at("Author_Download").at("operation_args");
        require(action == before_locale, "DOWNLOAD_LOCALIZE_CHANGED_CANONICAL_ACTION");
        require(action.at("retry").at("interval_ms") == 3000, "DOWNLOAD_RETRY_INTERVAL_CHANGED");
        require(action.at("retry").at("max_submissions") == 0, "DOWNLOAD_RETRY_LIMIT_CHANGED");
        require(action.at("postcondition_timeout_ms") == graph.time_limit.count(), "DOWNLOAD_DEADLINE_CHANGED");
        const auto ready = library.resource_condition("wheel.title", locale, wvd::authoring::ResourceUse::Observation);
        Ports evaluator(locale, "condition", ready);
        const auto retry = action.at("retry").at("ready").at("parameters");
        Surface pure;
        require(evaluator.evaluate(action.at("scene_recognition").at("parameters"), pure), "DOWNLOAD_FIRST_SCENE_CHANGED");
        require(evaluator.evaluate(action.at("target_recognition").at("parameters"), pure), "DOWNLOAD_TARGET_CHANGED");
        require(!evaluator.evaluate(action.at("postcondition").at("parameters"), pure), "DOWNLOAD_SAME_PAGE_FALSE_COMPLETION");
        require(evaluator.evaluate(retry, pure) == expect_fixed, "DOWNLOAD_EXPECTED_RETRY_CONTRADICTION_CHANGED");
        for (const auto *blocked : {"network", "pause", "gone"}) {
            auto frame = pure;
            if (std::string(blocked) == "network") frame.network = true;
            if (std::string(blocked) == "pause") frame.pause = true;
            if (std::string(blocked) == "gone") frame.download = false;
            require(!evaluator.evaluate(retry, frame), "DOWNLOAD_RETRY_GUARD_WIDENED");
        }
        // Ordinary wheel-menu retry must retain its input_clear gate.
        const auto ordinary = graph.nodes.at("Author_Wheel").at("operation_args").at("retry").at("ready");
        require(ordinary.dump().find("input_clear") != std::string::npos, "ORDINARY_MENU_BLOCKERS_REMOVED");
        conditions.push_back({{"locale", locale}, {"same_page_retry", evaluator.evaluate(retry, pure)},
            {"network_overlay_retry", false}, {"pause_overlay_retry", false}, {"gone_retry", false},
            {"ordinary_menu_still_uses_input_clear", true}});
        snapshots.push_back({{"locale", locale}, {"operation_args_before_locale", before_locale},
            {"operation_args_after_locale", action}, {"time_limit_ms", graph.time_limit.count()},
            {"download_node", graph.nodes.at("Author_Download")}});
        for (const auto *scenario : {"lost_first", "network_overlay", "pause_overlay", "target_disappeared", "delivery_unknown"})
            cases.push_back(std::make_unique<Case>(locale, scenario, graph, ready));

        // Exact-contract applicability: renaming a flow keeps the fix; changing
        // offset, threshold, source identity or completion keeps the normal gate.
        for (const auto *variant : {"renamed", "offset", "threshold", "scene", "post"}) {
            auto document = docs.at("wheel-open");
            document["flow"]["id"] = std::string("download-contract-") + variant;
            for (auto &node : document["nodes"]) if (node.at("id") == "Download") {
                auto &p = node["parameters"];
                if (std::string(variant) == "offset") p["offset"] = {1, 0};
                if (std::string(variant) == "threshold") {
                    p["scene"] = action.at("scene_recognition").at("parameters");
                    p["scene"]["threshold"] = .79;
                    p["target"] = p["scene"];
                    p["postcondition"] = C::absent(p["target"]);
                }
                if (std::string(variant) == "scene") p["scene"] = {{"mode", "all"}, {"conditions", {p["scene"], J{{"mode", "pause"}}}}};
                if (std::string(variant) == "post") p["postcondition"] = {{"mode", "semantic"}, {"id", "wheel.title"}};
            }
            const auto compiled = library.compile(document, {}, J::object(), locale).workflow;
            const auto &r = compiled.nodes.at("Author_Download").at("operation_args").at("retry").at("ready");
            const auto fixed_contract = expect_fixed && std::string(variant) == "renamed";
            require((r.dump().find("input_clear") == std::string::npos) == fixed_contract,
                "DOWNLOAD_SPECIAL_CASE_NOT_EXACT_CONTRACT");
        }
    }
    write(output / "compiled_download_requests.json", snapshots);
    write(output / "condition_assertions.json", conditions);
    const auto started = Clock::now(), deadline = started + 3400ms;
    for (auto &test : cases) test->ports.began = started;
    while (Clock::now() < deadline) {
        for (auto &test : cases) if (!terminal(test->last.state)) test->last = test->executor.tick();
        std::this_thread::sleep_for(2ms);
    }
    J runtime = J::array();
    for (auto &test : cases) {
        const auto &name = test->scenario;
        const auto sends = test->ports.inputs.size();
        const bool done = test->last.state == wvd::runtime::TickState::Completed;
        if (name == "lost_first" && expect_fixed) {
            require(done && sends == 2 && !test->executor.has_unresolved_input(), "DOWNLOAD_LOST_FIRST_NOT_CLOSED");
            require(test->ports.submitted_ms[1] - test->ports.submitted_ms[0] >= 3000, "DOWNLOAD_RETRIED_TOO_SOON");
            require(test->ports.inputs[1].at("frame_id") > test->ports.inputs[0].at("frame_id"), "DOWNLOAD_REUSED_OLD_FRAME");
        } else if (name == "target_disappeared") {
            require(done && sends == 1 && !test->executor.has_unresolved_input(), "DOWNLOAD_REPEATED_AFTER_EXIT");
        } else {
            require(!terminal(test->last.state) && sends == 1 && test->executor.has_unresolved_input(),
                "DOWNLOAD_BLOCKED_OR_UNKNOWN_REPLAYED");
        }
        runtime.push_back({{"locale", test->locale}, {"scenario", name}, {"state", state_name(test->last.state)},
            {"step", test->executor.current_step_id()}, {"code", test->last.code},
            {"inputs", test->ports.inputs}, {"input_results", test->ports.results},
            {"pending", test->executor.has_unresolved_input()}, {"progress", test->executor.progress_snapshot()}});
        std::cout << test->locale << ' ' << name << " state=" << state_name(test->last.state) << " inputs=" << sends << '\n';
    }
    write(output / "runtime_results.json", runtime);
    write(output / "scope.json", {{"expected_fixed", expect_fixed},
        {"scope", "actual public wheel-open document + production compiler + FlowExecutor; controlled per-frame leaf outcomes and memory submit"},
        {"observation_ms", 3400}, {"runtime_cases", cases.size()},
        {"not_executed", {"recognition Service", "NativeInputGate", "Windows build", "saved user workflow", "device", "game"}}});
    return 0;
}
} // namespace download_contract_test
