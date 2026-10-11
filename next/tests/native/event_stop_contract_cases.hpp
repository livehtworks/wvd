#pragma once

#include "authoring/workflow_validator.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "workflow/program.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

// These cases enter the production author/library compiler, compiled-workflow
// validator and native FlowProgram validator. No device or recognition outcome
// is supplied, and no invalid rule is repaired or stripped by the fixture.
namespace event_stop_contract {
using namespace wvd;
using namespace std::chrono_literals;
using J = nlohmann::json;
using C = games::tasks::PipelineCompiler;

inline void check(bool value, const std::string &reason) {
    if (!value) throw std::runtime_error("EVENT_STOP_CASE:" + reason);
}
struct Results {
    unsigned count{}, failed{};
    void expect(const std::string &name, const std::string &prefix,
                const std::string &suffix, const std::function<void()> &operation) {
        std::string error;
        try { operation(); } catch (const std::exception &e) { error = e.what(); }
        const bool pass = prefix.empty() ? error.empty()
            : error.starts_with(prefix) && error.ends_with(suffix);
        ++count; failed += !pass;
        std::cout << J{{"case", name}, {"pass", pass}, {"accepted", error.empty()},
            {"error", error}, {"expected_prefix", prefix}, {"expected_suffix", suffix}}.dump() << '\n';
    }
};
inline recognition::Request request() {
    return {"stop-proof", "1", {0, 0, 900, 1600},
        recognition::CustomParameters{"StopProof", J::object()}};
}
inline workflow::Step step(const std::string &id, workflow::StepData data,
                           std::vector<std::string> next = {}) {
    workflow::Step value;
    value.id = id; value.source_path = J::array({J{{"flow_id", "stop-ir"}, {"node_id", id}}}).dump();
    value.data = std::move(data); value.next = std::move(next);
    return value;
}
inline workflow::FlowProgram native_program(bool definition_scope) {
    workflow::FlowProgram p; p.revision = "stop-contract"; p.root_definition = "root";
    workflow::Definition root; root.id = "root"; root.entry = "start";
    root.steps.emplace("start", step("start", workflow::Route{}, {"done"}));
    root.steps.emplace("done", step("done", workflow::Finish{}));
    workflow::EventRule stop;
    stop.id = "manual-stop"; stop.priority = 1000; stop.detect = request();
    stop.disposition = workflow::EventDisposition::ExternalBlocked;
    stop.reason = "manual_required";
    if (definition_scope) root.events.push_back(stop);
    else root.steps.at("start").event_policy.push_back(stop);
    workflow::Definition handler; handler.id = "handler"; handler.entry = "return";
    handler.steps.emplace("return", step("return", workflow::Return{"completed", {}, {}}));
    p.definitions.emplace(root.id, std::move(root));
    p.definitions.emplace(handler.id, std::move(handler));
    return p;
}
inline workflow::EventRule &native_rule(workflow::FlowProgram &p, bool definition_scope) {
    auto &root = p.definitions.at("root");
    return definition_scope ? root.events.front() : root.steps.at("start").event_policy.front();
}
inline void native_cases(Results &results) {
    for (const bool definition_scope : {false, true}) {
        const std::string scope = definition_scope ? "definition" : "step";
        for (const auto *field : {"handler_definition", "on_device_restart", "resume",
                                 "replan_step", "resume_guard", "reason"}) {
            results.expect("native_" + scope + "_reject_" + field,
                "FLOW_EVENT_STOP_INVALID:root:", std::string(":manual-stop:") + field, [&] {
                    auto p = native_program(definition_scope);
                    auto &rule = native_rule(p, definition_scope);
                    if (std::string(field) == "handler_definition") rule.handler_definition = "handler";
                    if (std::string(field) == "on_device_restart") rule.on_device_restart = true;
                    if (std::string(field) == "resume") {
                        rule.resume = workflow::ResumeMode::Replan;
                        rule.replan_step = "start"; rule.resume_guard = request();
                    }
                    if (std::string(field) == "replan_step") rule.replan_step = "start";
                    if (std::string(field) == "resume_guard") rule.resume_guard = request();
                    if (std::string(field) == "reason") rule.reason.clear();
                    p.validate();
                });
        }
        results.expect("native_" + scope + "_direct_stop", {}, {}, [&] {
            native_program(definition_scope).validate();
        });
        for (const auto *kind : {"reobserve", "replan", "device_restart"})
            results.expect("native_" + scope + "_handled_" + kind, {}, {}, [&] {
                auto p = native_program(definition_scope);
                auto &rule = native_rule(p, definition_scope);
                rule.disposition = workflow::EventDisposition::Handle;
                rule.handler_definition = "handler"; rule.reason.clear();
                if (std::string(kind) != "reobserve") {
                    rule.resume = workflow::ResumeMode::Replan;
                    rule.replan_step = "start"; rule.resume_guard = request();
                }
                rule.on_device_restart = std::string(kind) == "device_restart";
                p.validate();
            });
    }
}

inline J descriptor() {
    return {{"id", "manual-stop"}, {"source_node", "Entry"}, {"class", "overlay"},
        {"priority", 1000}, {"detect", C::image("stop-marker")},
        {"disposition", "external_blocked"}, {"reason", "manual_required"}};
}
inline games::tasks::CompiledWorkflow pipeline(bool object_scope) {
    C child("stop-child"); child.route("Entry", {"Terminal"});
    C parent("stop-parent");
    const auto target = parent.define_child("Child", child.finish());
    parent.call_child("Entry", target, {"Terminal"});
    const J rules = J::array({descriptor()});
    parent.event_scope("Entry", object_scope ? J{{"rules", rules}, {"disabled", J::array()}} : rules);
    return parent.finish();
}
inline J &pipeline_rule(games::tasks::CompiledWorkflow &source, bool object_scope) {
    auto &scope = source.event_scopes.at("Entry");
    return object_scope ? scope.at("rules").front() : scope.front();
}
inline void pipeline_cases(Results &results) {
    for (const bool object_scope : {false, true}) {
        const std::string scope = object_scope ? "object" : "array";
        for (const auto *field : {"entry", "local_counters", "resume", "on_device_restart", "reason"})
            results.expect("pipeline_" + scope + "_reject_" + field,
                "COMPILE_EVENT_STOP_INVALID:Entry:manual-stop:", field, [&] {
                    auto source = pipeline(object_scope);
                    auto &rule = pipeline_rule(source, object_scope);
                    if (std::string(field) == "entry") rule["entry"] = "Child_Entry";
                    if (std::string(field) == "local_counters") rule["local_counters"] = J::array();
                    if (std::string(field) == "resume") rule["resume"] = {{"mode", "reobserve"}};
                    if (std::string(field) == "on_device_restart") rule["on_device_restart"] = true;
                    if (std::string(field) == "reason") rule["reason"] = "";
                    (void)games::tasks::compile_native_program(source, J::object(), "pipeline-stop-contract");
                });
        results.expect("pipeline_" + scope + "_direct_stop", {}, {}, [&] {
            auto source = pipeline(object_scope);
            pipeline_rule(source, object_scope)["on_device_restart"] = false;
            const auto p = games::tasks::compile_native_program(source, J::object(), "pipeline-stop-contract");
            const auto &rule = p.definitions.at(p.root_definition).steps.at("Entry").event_policy.front();
            check(rule.disposition == workflow::EventDisposition::ExternalBlocked &&
                rule.handler_definition.empty() && rule.reason == "manual_required", "pipeline stop changed");
        });
        results.expect("pipeline_" + scope + "_handled_restart", {}, {}, [&] {
            auto source = pipeline(object_scope);
            auto &rule = pipeline_rule(source, object_scope);
            rule["disposition"] = "handled"; rule.erase("reason");
            rule["entry"] = "Child_Entry"; rule["local_counters"] = J::array();
            rule["on_device_restart"] = true;
            rule["resume"] = {{"mode", "replan"}, {"node_id", "Entry"}, {"guard", C::image("stop-marker")}};
            const auto p = games::tasks::compile_native_program(source, J::object(), "pipeline-stop-contract");
            const auto &event = p.definitions.at(p.root_definition).steps.at("Entry").event_policy.front();
            check(event.disposition == workflow::EventDisposition::Handle && event.on_device_restart &&
                event.handler_definition == "Child_Entry" && event.resume_guard.has_value(), "handler metadata lost");
        });
    }
}

inline J document(const std::string &id) {
    return {{"schema", 1}, {"flow", {{"id", id}, {"name", id}, {"description", ""}}},
        {"entry", "Start"}, {"nodes", J::array({
            J{{"id", "Start"}, {"type", "route"}, {"name", "Start"}, {"parameters", J::object()}},
            J{{"id", "Done"}, {"type", "end"}, {"name", "Done"}, {"parameters", {{"outcome", "success"}}}}})},
        {"edges", J::array({J{{"id", "start-done"}, {"from", "Start"}, {"to", "Done"},
            {"outcome", "success"}, {"order", 0}}})},
        {"layout", {{"nodes", J::array({J{{"node_id", "Start"}, {"x", 0}, {"y", 0}},
                J{{"node_id", "Done"}, {"x", 200}, {"y", 0}}})},
            {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
        {"execution", {{"time_limit_ms", 10000},
            {"checks", {{"phase", "business"}, {"inherit", J::array({"manual-stop"})}}}}}};
}
inline J author_rule(bool handled = false) {
    J rule{{"enabled", true}, {"class", "overlay"}, {"priority", 1000},
        {"detect", C::image("stop-marker")}};
    if (handled) rule["handler"] = {{"flow_id", "stop-handler"}};
    else rule.update({{"disposition", "external_blocked"}, {"reason", "manual_required"}});
    return rule;
}
inline workflow::FlowProgram compile_documents(const J &documents) {
    games::tasks::PublicFlowLibrary library(documents);
    const auto compiled = library.compile(documents.at("stop-parent"), {});
    return games::tasks::compile_native_program(compiled.workflow, compiled.source_paths, "author-stop-contract");
}
inline bool contains_rule(const workflow::FlowProgram &program,
                          const std::function<bool(const workflow::EventRule &)> &predicate) {
    for (const auto &[id, definition] : program.definitions) {
        (void)id;
        for (const auto &rule : definition.events) if (predicate(rule)) return true;
        for (const auto &[step_id, node] : definition.steps) {
            (void)step_id;
            for (const auto &rule : node.event_policy) if (predicate(rule)) return true;
        }
    }
    return false;
}
inline J author_documents(bool inherited, bool handled = false) {
    auto root = document("stop-parent");
    root["execution"]["events"] = {{"manual-stop", author_rule(handled)}};
    J docs{{"stop-parent", root}, {"stop-handler", document("stop-handler")}};
    if (inherited) {
        docs["stop-child"] = document("stop-child");
        auto &entry = docs["stop-parent"]["nodes"][0];
        entry["type"] = "call"; entry["parameters"] = {{"flow_id", "stop-child"}};
    }
    return docs;
}
inline void author_cases(Results &results) {
    for (const bool inherited : {false, true}) {
        const std::string scope = inherited ? "inherited" : "declared";
        const std::string flow = inherited ? "stop-child" : "stop-parent";
        for (const auto *kind : {"arguments", "reobserve", "replan", "disabled_arguments"}) {
            const std::string field = std::string(kind).find("arguments") != std::string::npos ? "handler" : "resume";
            results.expect("author_" + scope + "_reject_" + kind,
                "EVENT_BLOCKED_INVALID:" + flow + ":Start:manual-stop:", field, [&] {
                    auto docs = author_documents(inherited);
                    auto &node = docs[flow]["nodes"][0];
                    if (field == "handler") {
                        node["event_overrides"] = {{"manual-stop", {{"arguments", J::object()}}}};
                        if (std::string(kind) == "disabled_arguments")
                            node["event_overrides"]["manual-stop"]["enabled"] = false;
                    } else {
                        node["resume"] = {{"manual-stop", {{"mode", kind}}}};
                        if (std::string(kind) == "replan")
                            node["resume"]["manual-stop"].update({{"node_id", "Start"}, {"guard", C::image("stop-marker")}});
                    }
                    (void)compile_documents(docs);
                });
        }
        results.expect("author_" + scope + "_direct_stop", {}, {}, [&] {
            auto docs = author_documents(inherited);
            // An empty nested list remains compatible with existing documents.
            docs["stop-parent"]["execution"]["events"]["manual-stop"]["allow_nested"] = J::array();
            const auto p = compile_documents(docs);
            check(contains_rule(p, [](const auto &rule) {
                return rule.id == "manual-stop" && rule.disposition == workflow::EventDisposition::ExternalBlocked &&
                    rule.reason == "manual_required" && rule.handler_definition.empty() && !rule.resume_guard;
            }), "author direct stop missing");
        });
        results.expect("author_" + scope + "_handled_arguments_and_replan", {}, {}, [&] {
            auto docs = author_documents(inherited, true);
            auto &node = docs[flow]["nodes"][0];
            node["event_overrides"] = {{"manual-stop", {{"arguments", J::object()}}}};
            node["resume"] = {{"manual-stop", {{"mode", "replan"}, {"node_id", "Start"},
                {"guard", C::image("stop-marker")}}}};
            const auto p = compile_documents(docs);
            check(contains_rule(p, [](const auto &rule) {
                return rule.id == "manual-stop" && rule.disposition == workflow::EventDisposition::Handle &&
                    !rule.handler_definition.empty() && rule.resume == workflow::ResumeMode::Replan && rule.resume_guard;
            }), "author handler continuation missing");
        });
    }
    results.expect("author_declared_stop_reject_nested_handler", "EVENT_BLOCKED_INVALID:manual-stop:", "allow_nested", [&] {
        auto docs = author_documents(false);
        docs["stop-parent"]["execution"]["events"]["manual-stop"]["allow_nested"] = {"secondary-handler"};
        (void)compile_documents(docs);
    });
    results.expect("author_inherited_stop_enabled_override_only", {}, {}, [&] {
        auto docs = author_documents(true);
        docs["stop-child"]["nodes"][0]["event_overrides"] = {{"manual-stop", {{"enabled", false}}}};
        const auto p = compile_documents(docs);
        bool disabled = false;
        for (const auto &[id, definition] : p.definitions) {
            (void)id;
            for (const auto &[step_id, node] : definition.steps) {
                (void)step_id;
                if (node.source_path.find("stop-child") != std::string::npos &&
                    node.disabled_events.contains("manual-stop")) disabled = true;
            }
        }
        check(disabled, "explicit child suppression changed");
    });
    results.expect("author_child_replaces_stop_with_explicit_handler", {}, {}, [&] {
        auto docs = author_documents(true);
        docs["stop-child"]["execution"]["events"] = {{"manual-stop", author_rule(true)}};
        const auto p = compile_documents(docs);
        check(contains_rule(p, [](const auto &rule) {
            return rule.id == "manual-stop" && rule.disposition == workflow::EventDisposition::Handle &&
                !rule.handler_definition.empty();
        }), "explicit handler declaration rejected");
    });
}

inline int run() {
    Results results;
    native_cases(results);
    pipeline_cases(results);
    author_cases(results);
    std::cout << J{{"summary", "event-stop-contract"}, {"cases", results.count},
        {"failed", results.failed}, {"pass", results.failed == 0},
        {"scope", "author/library compiler and native validation; no Service or device execution"}}.dump() << '\n';
    return results.failed ? 1 : 0;
}
} // namespace event_stop_contract
