#include "author_workflow.hpp"
#include "authoring/document_parameters.hpp"
#include "authoring/event_policy.hpp"
#include "authoring/resource_locale.hpp"
#include "authoring/workflow_validator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace wvd::games::tasks {
namespace {
using J = nlohmann::json;

[[noreturn]] void fail(const std::string &code, const std::string &identity = {}) {
    throw std::runtime_error(identity.empty() ? code : code + ":" + identity);
}


std::string pipeline_name(const std::string &node_id, const std::string &entry,
                          const std::string &success_end) {
    (void)entry; // Entry 留给无副作用的根分发，作者首节点必须参与真实识别。
    if (node_id == success_end)
        return "Terminal";
    return "Author_" + node_id;
}

J successors(const std::vector<const J *> &edges, const std::string &entry,
             const std::string &success_end) {
    J result = J::array();
    for (const auto *edge : edges)
        result.push_back(pipeline_name(edge->at("to").get<std::string>(), entry, success_end));
    return result;
}
} // namespace

void validate_author_workflow(const J &document) {
    authoring::validate_author_workflow(document);
}

AuthorWorkflowCompilation compile_author_workflow(const J &source,
                                                  AuthorBusinessResolver resolver,
                                                  AuthorFlowResolver public_flow,
                                                  AuthorScopedFlowResolver scoped_flow,
                                                  J inherited_events,
                                                  std::optional<std::set<std::string>> allowed_events) {
    const auto document = authoring::instantiate_document(source);
    try {
        validate_author_workflow(document);
    } catch (const nlohmann::json::exception &error) {
        fail("AUTHOR_EVENT_DOCUMENT_JSON_INVALID", error.what());
    }
    // symbolic 只允许存在于草稿；必须经 PublicFlowLibrary 绑定语言和使用语义再编译。
    const auto unresolved = [&](auto &&self, const J &v) -> bool {
        if (v.is_object()) {
            if (v.contains("mode") && v.at("mode").is_string() &&
                (v.at("mode") == "semantic" || v.at("mode") == "location")) return true;
            for (const auto &child : v) if (self(self, child)) return true;
        } else if (v.is_array()) for (const auto &child : v) if (self(self, child)) return true;
        return false;
    };
    if (unresolved(unresolved, document.at("nodes"))) fail("AUTHOR_SEMANTIC_UNRESOLVED");
    authoring::ValidatedGraph graph;
    try {
        graph = authoring::validate_graph(document);
    } catch (const nlohmann::json::exception &error) {
        fail("AUTHOR_EVENT_GRAPH_JSON_INVALID", error.what());
    }
    const auto entry = document.at("entry").get<std::string>();
    const auto flow_id = document.at("flow").at("id").get<std::string>();
    const auto budget = std::chrono::milliseconds{
        document.at("execution").at("time_limit_ms").get<std::int64_t>()};
    PipelineCompiler compiler("author." + flow_id, budget);
    AuthorWorkflowCompilation result;
    compiler.route("Entry", {pipeline_name(entry, entry, graph.success_end)});
    if (!graph.failure.at(entry).empty())
        compiler.failure_route("Entry", successors(graph.failure.at(entry), entry, graph.success_end));

    std::size_t public_ordinal{};
    std::map<std::string, J> event_handlers;
    J entry_events = J::array();
    J entry_disabled = J::array();
    for (const auto &[id, node] : graph.nodes) {
        const auto runtime_name = pipeline_name(id, entry, graph.success_end);
        result.node_to_pipeline[id] = {runtime_name};
        result.source_paths[runtime_name] = J::array({J{{"flow_id", flow_id}, {"node_id", id}}});
        if (!result.pipeline_to_node.emplace(runtime_name, id).second)
            fail("AUTHOR_PIPELINE_MAPPING_DUPLICATE", id + ":" + runtime_name);
        if (id == graph.success_end)
            continue;
        const auto next = successors(graph.success.at(id), entry, graph.success_end);
        const auto &parameters = node->at("parameters");
        const auto type = node->at("type").get<std::string>();
        try {
            if (type == "route") {
                compiler.route(runtime_name, next);
            } else if (type == "recognition") {
                const auto &condition = parameters.at("condition");
                if (condition.value("mode", "") == "ocr")
                    compiler.observe_ocr(runtime_name,
                                         condition.at("expected").get<std::vector<std::string>>(),
                                         condition.value("roi", J::array({0, 0, 900, 1600})), next);
                else
                    compiler.observe(runtime_name, condition, next);
            } else if (type == "action") {
                const auto operation = parameters.at("operation").get<std::string>();
                if (operation == "click")
                    compiler.click(runtime_name, parameters.at("scene"), parameters.at("target"),
                                   parameters.at("postcondition"), next,
                                   parameters.value("offset", J::array({0, 0})));
                else if (operation == "fixed_click")
                    compiler.fixed_click(runtime_name, parameters.at("scene"),
                                         parameters.at("postcondition"),
                                         parameters.at("position"), next);
                else if (operation == "back")
                    compiler.back(runtime_name, parameters.at("scene"),
                                  parameters.at("postcondition"), next);
                else if (operation == "swipe")
                    compiler.swipe(runtime_name, parameters.at("scene"),
                                   parameters.at("postcondition"),
                                   parameters.at("coordinates"), next,
                                   static_cast<int>(parameters.at("duration_ms").get<std::int64_t>()));
                else
                    fail("AUTHOR_ACTION_UNSUPPORTED", id + ":" + operation);
                if (parameters.contains("allowed_area"))
                    compiler.allowed_area(runtime_name, parameters.at("allowed_area"));
                if (parameters.contains("postcondition_timeout_ms"))
                    compiler.postcondition_budget(
                        runtime_name,
                        static_cast<int>(parameters.at("postcondition_timeout_ms").get<std::int64_t>()));
            } else if (type == "wait") {
                // 等待由可取消的原生步骤持有，不阻塞停止链。
                compiler.wait(runtime_name,
                              static_cast<int>(parameters.at("duration_ms").get<std::int64_t>()),
                              next);
            } else if (type == "call" || type == "slot") {
                const J calls = type == "call" ? J::array({parameters})
                                               : parameters.value("calls", J::array());
                const auto add_call = [&](const J &call, const std::string &name, J successors) {
                    if (!public_flow && !scoped_flow) fail("AUTHOR_FLOW_CONTEXT_REQUIRED", id);
                    const auto scope = authoring::effective_event_policy(inherited_events, document, *node);
                    const auto child = scoped_flow ? scoped_flow(call, scope, allowed_events) : public_flow(call);
                    const auto prefix = "Public" + std::to_string(public_ordinal++);
                    const auto child_entry = compiler.define_child(prefix, child.workflow);
                    compiler.call_child(name, child_entry, std::move(successors));
                    for (const auto &[child_name, unused] : child.workflow.nodes.items()) {
                        (void)unused;
                        const auto compiled_name = prefix + "_" + child_name;
                        result.node_to_pipeline[id].push_back(compiled_name);
                        result.pipeline_to_node[compiled_name] = id;
                        J path = J::array({J{{"flow_id", flow_id}, {"node_id", id}}});
                        if (child.source_paths.contains(child_name))
                            for (const auto &part : child.source_paths.at(child_name)) path.push_back(part);
                        result.source_paths[compiled_name] = std::move(path);
                    }
                };
                if (type == "call") add_call(parameters, runtime_name, next);
                else {
                    compiler.route(runtime_name, calls.empty() ? next : J::array({runtime_name + "_Call0"}));
                    for (std::size_t i = 0; i < calls.size(); ++i) {
                        const auto name = runtime_name + "_Call" + std::to_string(i);
                        add_call(calls.at(i), name, i + 1 == calls.size() ? next
                            : J::array({runtime_name + "_Call" + std::to_string(i + 1)}));
                        compiler.hit_limit(name, node->contains("repeat_limit")
                            ? static_cast<int>(node->at("repeat_limit").get<std::int64_t>()) : 1);
                        result.node_to_pipeline[id].push_back(name);
                        result.pipeline_to_node[name] = id;
                        result.source_paths[name] = J::array({J{{"flow_id", flow_id}, {"node_id", id}}});
                    }
                }
            } else if (type == "business") {
                const auto binding = parameters.at("binding").get<std::string>();
                if (binding == "confirm")
                    compiler.confirm(
                        runtime_name, parameters.at("operation_id").get<std::string>(),
                        parameters.at("event").get<std::string>(), parameters.at("condition"), next,
                        parameters.contains("expected_step") ? parameters.at("expected_step") : J(nullptr));
                else if (binding == "combat" || binding == "chest" || binding == "task_stage") {
                    if (!resolver)
                        fail("AUTHOR_BUSINESS_CONTEXT_REQUIRED", id);
                    const auto prefix = runtime_name + "_Business";
                    const auto child = resolver(parameters);
                    const auto child_entry = compiler.define_child(prefix, child);
                    compiler.call_child(runtime_name, child_entry, next);
                }
                else
                    fail("AUTHOR_BUSINESS_UNSUPPORTED", id + ":" + binding);
            } else if (type == "end") {
                compiler.business_failure(runtime_name, parameters.at("reason").get<std::string>());
            } else {
                fail("AUTHOR_NODE_TYPE_INVALID", id + ":" + type);
            }
            if (type != "end") {
                const auto effective = authoring::effective_event_policy(inherited_events, document, *node);
                J active = J::array();
                J disabled = J::array();
                const auto declared = document.at("execution").value("events", J::object());
                const auto overrides = node->value("event_overrides", J::object());
                const auto resumes = node->value("resume", J::object());
                for (const auto &[event_id, rule] : effective.items()) {
                    // task_stage 内的 Dispatch 已负责遇怪/开箱；作者层不能再接管同一次遭遇。
                    if (type == "business" && parameters.value("binding", "") == "task_stage" &&
                        rule.value("class", "") == "encounter") {
                        disabled.push_back(event_id);
                        continue;
                    }
                    if (!rule.value("enabled", false)) {
                        disabled.push_back(event_id);
                        continue;
                    }
                    if (allowed_events && !allowed_events->contains(event_id)) {
                        if ((declared.contains(event_id) && declared.at(event_id).value("enabled", false)) ||
                            (overrides.contains(event_id) && overrides.at(event_id).value("enabled", false)))
                            fail("EVENT_NESTED_NOT_ALLOWED", id + ":" + event_id);
                        disabled.push_back(event_id);
                        continue;
                    }
                    // 未改写的祖先规则由调用帧持有；复制到子定义会把 replan 目标误归给子帧。
                    if (!declared.contains(event_id) && !overrides.contains(event_id) &&
                        !resumes.contains(event_id)) continue;
                    if (!declared.contains(event_id) && overrides.contains(event_id) &&
                        rule.value("resume", J::object()).value("mode", "") == "replan" &&
                        !resumes.contains(event_id))
                        fail("EVENT_REPLAN_OWNER_OVERRIDE_REQUIRED", id + ":" + event_id);
                    if (unresolved(unresolved, rule.at("detect")))
                        fail("EVENT_SEMANTIC_UNRESOLVED", event_id);
                    const auto resume = rule.value("resume", J{{"mode", "reobserve"}});
                    if (resume.value("mode", "") == "replan") {
                        const auto target = resume.at("node_id").get<std::string>();
                        if (!graph.nodes.contains(target) || graph.nodes.at(target)->at("type") == "end" ||
                            unresolved(unresolved, resume.at("guard")))
                            fail("EVENT_REPLAN_INVALID", event_id + ":" + target);
                    }
                    J descriptor{{"id", event_id}, {"class", rule.at("class")},
                                 {"priority", rule.at("priority")}, {"detect", rule.at("detect")},
                                 {"disposition", rule.value("disposition", "handled")},
                                 {"source_node", runtime_name}};
                    if (descriptor.at("disposition") == "external_blocked") {
                        descriptor["reason"] = rule.at("reason");
                    } else {
                        if (!scoped_flow) fail("EVENT_HANDLER_CONTEXT_REQUIRED", event_id);
                        const auto nested_policy = authoring::nested_event_policy(effective, event_id);
                        const auto &nested = nested_policy.rules;
                        const auto &permitted = nested_policy.direct;
                        const auto key = event_id + rule.at("handler").dump() + nested.dump();
                        if (!event_handlers.contains(key)) {
                            const auto child = scoped_flow(rule.at("handler"), nested, permitted);
                            const auto prefix = "Event" + std::to_string(public_ordinal++);
                            const auto child_entry = compiler.define_child(prefix, child.workflow);
                            J reset = J::array();
                            for (const auto &[child_name, unused] : child.workflow.nodes.items()) {
                                (void)unused;
                                const auto compiled_name = prefix + "_" + child_name;
                                reset.push_back(compiled_name);
                                J path = J::array({J{{"event_id", event_id}}});
                                if (child.source_paths.contains(child_name))
                                    for (const auto &part : child.source_paths.at(child_name)) path.push_back(part);
                                result.source_paths[compiled_name] = std::move(path);
                            }
                            event_handlers[key] = {{"entry", child_entry}, {"local_counters", reset}};
                        }
                        descriptor.update(event_handlers.at(key));
                        descriptor["resume"] = resume;
                        if (resume.value("mode", "") == "replan")
                            descriptor["resume"]["node_id"] = pipeline_name(
                                resume.at("node_id").get<std::string>(), entry, graph.success_end);
                    }
                    active.push_back(std::move(descriptor));
                }
                if (!active.empty() || !disabled.empty()) {
                    if (id == entry) {
                        entry_events = active;
                        entry_disabled = disabled;
                    }
                    compiler.event_scope(runtime_name,
                        {{"rules", std::move(active)}, {"disabled", std::move(disabled)}});
                }
            }
            if (node->contains("repeat_limit"))
                compiler.hit_limit(runtime_name,
                                   static_cast<int>(node->at("repeat_limit").get<std::int64_t>()));
            else
                compiler.hit_limit(runtime_name, 1);
            if (type != "wait" && parameters.contains("delay_after_ms"))
                compiler.delay_after(runtime_name,
                                     static_cast<int>(parameters.at("delay_after_ms").get<std::int64_t>()));
            if (!graph.failure.at(id).empty())
                compiler.failure_route(runtime_name,
                                       successors(graph.failure.at(id), entry, graph.success_end));
        } catch (const nlohmann::json::exception &error) {
            fail("AUTHOR_NODE_JSON_INVALID", id + ":" + error.what());
        } catch (const authoring::ContractError &) {
            throw;
        } catch (const std::runtime_error &error) {
            const std::string message = error.what();
            if (message.starts_with("AUTHOR_"))
                throw;
            fail("AUTHOR_NODE_COMPILE_FAILED", id + ":" + message);
        }
    }
    try {
    if (!entry_events.empty() || !entry_disabled.empty()) {
        for (auto &rule : entry_events) rule["source_node"] = "Entry";
        compiler.event_scope("Entry", {{"rules", std::move(entry_events)},
            {"disabled", std::move(entry_disabled)}});
    }
    result.workflow = compiler.finish();
    } catch (const nlohmann::json::exception &error) {
        fail("AUTHOR_FINISH_JSON_INVALID", error.what());
    } catch (const std::runtime_error &error) {
        fail("AUTHOR_COMPILE_FAILED", error.what());
    }
    // 编译器可能增加业务检查点和统一恢复节点；二者属于既有运行模型而非作者节点。
    for (const auto &[id, source_node] : graph.nodes) {
        (void)source_node;
        const auto prefix = pipeline_name(id, entry, graph.success_end) + "_Business_";
        for (const auto &[name, node] : result.workflow.nodes.items()) {
            (void)node;
            if (name.starts_with(prefix)) {
                result.node_to_pipeline[id].push_back(name);
                result.pipeline_to_node[name] = id;
            }
        }
    }
    result.workflow.validate();
    return result;
}

} // namespace wvd::games::tasks
