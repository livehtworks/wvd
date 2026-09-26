#include "native_program.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

namespace wvd::games::tasks {
namespace {
using J = nlohmann::json;
using workflow::Step;

std::vector<std::string> edges(const J &node, const char *field) {
    std::vector<std::string> result;
    for (const auto &edge : node.value(field, J::array()))
        result.push_back(edge.get<std::string>());
    return result;
}

recognition::Request request(const J &value) {
    return recognition::parse_request(value);
}

std::optional<recognition::Request> guard(const J &node, const std::string &id) {
    const auto kind = node.value("observation", "Always");
    if (kind == "Always") return std::nullopt;
    const auto roi = node.value("roi", J::array({0, 0, 900, 1600}));
    if (kind == "OCR")
        return request({{"id", id}, {"revision", "1"}, {"type", "ocr"},
                        {"roi", roi}, {"expected", node.at("expected")}});
    if (kind == "Registered" && node.value("recognizer", "") == "WvdVision")
        return request({{"id", id}, {"revision", "1"}, {"type", "custom"},
                        {"binding", "WvdVision"}, {"roi", roi},
                        {"parameters", node.at("observation_args")}});
    throw std::runtime_error("NATIVE_RECOGNITION_UNSUPPORTED:" + id);
}

std::string source_path(const J &paths, const std::string &id) {
    if (paths.is_object() && paths.contains(id)) return paths.at(id).dump();
    return J::array({J{{"native_node", id}}}).dump();
}

std::vector<workflow::EventRule> events(const J &rules, const std::string &source) {
    std::vector<workflow::EventRule> result;
    if (!rules.is_array()) throw std::runtime_error("NATIVE_EVENT_RULES_INVALID");
    for (const auto &rule : rules) {
        if (rule.at("source_node") != source)
            throw std::runtime_error("NATIVE_EVENT_SOURCE_INVALID");
        workflow::EventRule event;
        event.id = rule.at("id");
        const auto category = rule.at("class").get<std::string>();
        if (category == "overlay") event.category = workflow::EventClass::Overlay;
        else if (category == "encounter") event.category = workflow::EventClass::Encounter;
        else throw std::runtime_error("NATIVE_EVENT_CLASS_INVALID");
        event.priority = rule.at("priority").get<int>();
        event.exit_budget = std::chrono::milliseconds{rule.value("exit_budget_ms", 5000)};
        event.ambiguity_budget = std::chrono::milliseconds{
            rule.value("ambiguity_budget_ms", 5000)};
        event.detect = request({{"id", "event." + event.id}, {"revision", "1"},
            {"type", "custom"}, {"binding", "WvdVision"},
            {"roi", {0, 0, 900, 1600}}, {"parameters", rule.at("detect")}});
        const auto disposition = rule.value("disposition", "handled");
        if (disposition == "external_blocked") {
            event.disposition = workflow::EventDisposition::ExternalBlocked;
            event.reason = rule.at("reason");
        } else if (disposition == "handled") {
            event.handler_definition = rule.at("entry");
            const auto resume = rule.value("resume", J{{"mode", "reobserve"}});
            if (resume.value("mode", "") == "replan") {
                event.resume = workflow::ResumeMode::Replan;
                event.replan_step = resume.at("node_id");
                // 原作者规则的返回守卫不能在 lowering 中丢掉；并入同一运行快照。
                event.resume_guard = request({{"id", "event.resume." + event.id},
                    {"revision", "1"}, {"type", "custom"}, {"binding", "WvdVision"},
                    {"roi", {0, 0, 900, 1600}}, {"parameters", resume.at("guard")}});
            } else if (resume.value("mode", "") != "reobserve")
                throw std::runtime_error("NATIVE_EVENT_RESUME_INVALID");
        } else throw std::runtime_error("NATIVE_EVENT_DISPOSITION_INVALID");
        result.push_back(std::move(event));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.priority > right.priority;
    });
    return result;
}

Step translate(const std::string &id, const J &node, const J &paths,
               const J &event_scopes, bool root_definition) {
    Step step;
    step.id = id;
    step.source_path = source_path(paths, id);
    step.guard = guard(node, id);
    step.next = edges(node, "next");
    step.on_error = edges(node, "on_error");
    step.handles_business_failure = node.value("business_failure_route", false);
    step.max_hit = node.value("max_hit", 1);
    step.time_limit = std::chrono::milliseconds{node.value("timeout", 60000LL)};
    step.delay_after = std::chrono::milliseconds{node.value("post_delay", 0LL)};
    if (event_scopes.contains(id)) {
        const auto &scope = event_scopes.at(id);
        if (scope.is_array()) step.event_policy = events(scope, id);
        else if (scope.is_object()) {
            step.event_policy = events(scope.at("rules"), id);
            for (const auto &disabled : scope.value("disabled", J::array()))
                step.disabled_events.insert(disabled.get<std::string>());
        } else throw std::runtime_error("NATIVE_EVENT_SCOPE_INVALID");
    }
    const auto action = node.value("operation", "Route");
    const auto binding = node.value("binding", "");
    if (action == "Route") {
        if (step.next.empty()) step.data = workflow::Return{"completed"};
        else if (step.guard) step.data = workflow::Observe{*step.guard};
        else step.data = workflow::Route{step.next};
    } else if (action == "Registered" && binding == "Input") {
        const auto &p = node.at("operation_args");
        const auto area = p.at("allowed_area").get<std::vector<int>>();
        if (area.size() != 4) throw std::runtime_error("NATIVE_ACTION_AREA_INVALID");
        workflow::Input input{request(p.at("scene_recognition")),
            request(p.at("target_recognition")), p.at("command"),
            {area[0], area[1], area[2], area[3]}, std::nullopt,
            p.value("use_target_center", true), p.value("clip_target_to_area", false)};
        if (p.contains("target_offset")) {
            const auto offset = p.at("target_offset").get<std::vector<int>>();
            if (offset.size() != 2) throw std::runtime_error("NATIVE_ACTION_OFFSET_INVALID");
            input.target_offset = contracts::Point{offset[0], offset[1]};
        }
        step.data = std::move(input);
    } else if (action == "Registered" && binding == "Call") {
        step.data = workflow::Call{node.at("operation_args").at("entry")};
    } else if (action == "Registered" && binding == "CancelableWait") {
        step.data = workflow::Wait{std::chrono::milliseconds{
            node.at("operation_args").at("duration_ms").get<std::int64_t>()}};
    } else if (action == "Registered" && binding == "Finish") {
        if (root_definition) step.data = workflow::Finish{};
        else step.data = workflow::Return{"completed"};
    } else if (action == "Registered" && binding == "RequireRecovery") {
        step.data = workflow::Fail{node.at("operation_args").at("reason")};
    } else if (action == "Registered" && binding == "AuthorBusinessFailure") {
        const auto reason = node.at("operation_args").at("reason").get<std::string>();
        if (root_definition) step.data = workflow::BusinessFail{reason};
        else step.data = workflow::Return{"failure", reason};
    } else if (action == "Registered" && binding == "WvdConfirm") {
        const auto &p = node.at("operation_args");
        step.data = workflow::BusinessConfirm{p.at("operation"),
            request(p.at("confirmation")), p, "WvdConfirm"};
    } else if (action == "Registered" &&
               (binding == "WvdCombat" || binding == "WvdChest" ||
                binding == "WvdUnknownLeap" || binding == "BusinessCheckpoint" ||
                binding == "BeginObservationPhase" || binding == "EndObservationPhase")) {
        step.data = workflow::RegisteredOperation{binding,
            node.value("operation_args", J::object())};
    } else {
        throw std::runtime_error("NATIVE_OPERATION_UNSUPPORTED:" + id + ":" + binding);
    }
    return step;
}

} // namespace

workflow::FlowProgram compile_native_program(const CompiledWorkflow &source,
                                             const J &source_paths,
                                             const std::string &revision) {
    source.validate();
    if (revision.empty()) throw std::runtime_error("NATIVE_PROGRAM_REVISION_MISSING");
    workflow::FlowProgram program;
    program.revision = revision;
    program.root_definition = source.entry;
    std::set<std::string> roots{source.entry};
    for (const auto &[id, node] : source.nodes.items()) {
        (void)id;
        if (node.value("binding", "") == "Call")
            roots.insert(node.at("operation_args").at("entry").get<std::string>());
    }
    for (const auto &[id, scope] : source.event_scopes.items()) {
        (void)id;
        const auto &rules = scope.is_array() ? scope : scope.at("rules");
        for (const auto &rule : rules)
            if (rule.contains("entry")) roots.insert(rule.at("entry").get<std::string>());
    }
    std::map<std::string, std::string> owner;
    for (const auto &root : roots) {
        workflow::Definition definition;
        definition.id = root;
        definition.entry = root;
        if (root == source.entry) definition.cumulative_budget = source.declared_budget;
        else if (const auto budget = source.definition_budgets.find(root);
                 budget != source.definition_budgets.end())
            definition.cumulative_budget = budget->second;
        std::set<std::string> seen;
        std::function<void(const std::string &)> visit = [&](const std::string &id) {
            if (!seen.insert(id).second) return;
            const auto &node = source.nodes.at(id);
            if (node.value("binding", "") != "RequireRecovery") {
                const auto [it, inserted] = owner.emplace(id, root);
                if (!inserted && it->second != root)
                    throw std::runtime_error("NATIVE_SCOPE_OVERLAP:" + id);
            }
            for (const auto *field : {"next", "on_error"})
                for (const auto &next : edges(node, field)) visit(next);
        };
        visit(root);
        for (const auto &id : seen) {
            auto translated = translate(id, source.nodes.at(id), source_paths, source.event_scopes,
                                        root == source.entry);
            if (source.nodes.at(id).value("pure_business_guard", false)) {
                translated.business_guard = source.nodes.at(id).at("observation_args");
                translated.guard.reset();
                translated.data = workflow::Route{};
            }
            definition.steps.emplace(id, std::move(translated));
        }
        program.definitions.emplace(root, std::move(definition));
    }
    for (auto &[root, definition] : program.definitions) {
        (void)root;
        for (auto &[id, step] : definition.steps) {
            if (!std::holds_alternative<workflow::Input>(step.data)) continue;
            const auto &p = source.nodes.at(id).at("operation_args");
            const auto await_id = id + "@await";
            if (definition.steps.contains(await_id))
                throw std::runtime_error("NATIVE_AWAIT_DUPLICATE");
            Step await;
            await.id = await_id;
            await.source_path = step.source_path;
            await.data = workflow::AwaitResult{request(p.at("postcondition")),
                std::chrono::milliseconds{p.value("transition_timeout_ms",
                    p.value("postcondition_timeout_ms", step.time_limit.count()))},
                step.delay_after, std::chrono::milliseconds{50}};
            await.next = std::move(step.next);
            await.on_error = step.on_error;
            await.time_limit = step.time_limit;
            // 输入和它的观察器是同一业务步骤；次数/作用域禁用必须完整继承。
            await.max_hit = step.max_hit;
            await.event_policy = step.event_policy;
            await.disabled_events = step.disabled_events;
            step.next = {await_id};
            step.delay_after = std::chrono::milliseconds{0};
            definition.steps.emplace(await_id, std::move(await));
        }
    }
    program.validate();
    return program;
}
} // namespace wvd::games::tasks
