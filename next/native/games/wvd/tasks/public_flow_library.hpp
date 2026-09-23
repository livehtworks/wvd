#pragma once
#include "author_workflow.hpp"
#include "authoring/document_parameters.hpp"
#include "authoring/event_policy.hpp"
#include "authoring/semantic_assets.hpp"

namespace wvd::games::tasks {
// 只组织/编译作者定义；执行仍为既有 RunCoordinator -> Maa Pipeline。
// Snapshot 必须由 WorkflowRepository 在同一读锁内取得，不在编译回调中回读活动文件。
class PublicFlowLibrary {
  public:
    using J = nlohmann::json;
    explicit PublicFlowLibrary(J documents, J semantic_catalogue = J::object())
        : documents_(std::move(documents)), semantic_catalogue_(std::move(semantic_catalogue)) {
        if (!documents_.is_object()) authoring::contract_error("FLOW_SNAPSHOT_INVALID");
        for (const auto &[id, document] : documents_.items()) {
            if (document.at("flow").at("id") != id) authoring::contract_error("FLOW_SNAPSHOT_ID_MISMATCH", id);
            validate_author_workflow(document);
        }
    }
    // 先收集实际绑定后的 task_stage，后选择同一份有效配置；不能漏掉嵌套块里的任务覆盖。
    std::set<std::string> task_profiles(const J &root, const J &args = J::object(), const std::string &locale = {}) const {
        std::set<std::string> ids;
        authoring::SemanticAssets assets(semantic_catalogue_);
        std::vector<std::string> active;
        std::size_t count{};
        const auto walk = [&](auto &&self, const J &source, const J &provided, const J &extensions,
                              const J &inherited, const std::optional<std::set<std::string>> &allowed) -> void {
            Enter enter(active, source.at("flow").at("id").get<std::string>(), count);
            const auto doc = assets.lower(authoring::instantiate_document(source, provided, extensions), locale);
            validate_author_workflow(doc);
            const auto child = [&](const J &call, const J &scope,
                                   const std::optional<std::set<std::string>> &nested) {
                authoring::validate_call(call);
                self(self, lookup(call), call.value("arguments", J::object()),
                     call.value("extensions", J::object()), scope, nested);
            };
            std::set<std::string> observed_handlers;
            for (const auto &node : doc.at("nodes")) {
                if (node.at("type") == "end") continue;
                const auto &p = node.at("parameters");
                const auto scope = authoring::effective_event_policy(inherited, doc, node);
                if (node.at("type") == "business" && p.value("binding", "") == "task_stage")
                    ids.insert(p.at("task_id").get<std::string>());
                if (node.at("type") == "call") child(p, scope, allowed);
                if (node.at("type") == "slot")
                    for (const auto &call : p.at("calls")) child(call, scope, allowed);
                for (const auto &[event_id, rule] : scope.items()) {
                    if (!rule.value("enabled", false)) continue;
                    if (allowed && !allowed->contains(event_id)) {
                        const auto declared = doc.at("execution").value("events", J::object());
                        const auto overrides = node.value("event_overrides", J::object());
                        if ((declared.contains(event_id) && declared.at(event_id).value("enabled", false)) ||
                            (overrides.contains(event_id) && overrides.at(event_id).value("enabled", false)))
                            authoring::contract_error("EVENT_NESTED_NOT_ALLOWED", event_id);
                        continue;
                    }
                    if (!rule.contains("handler")) continue;
                    const auto nested_policy = authoring::nested_event_policy(scope, event_id);
                    const auto &nested = nested_policy.rules;
                    const auto &permitted = nested_policy.direct;
                    const auto identity = event_id + rule.at("handler").dump() + nested.dump();
                    if (observed_handlers.insert(identity).second)
                        child(rule.at("handler"), nested, permitted);
                }
            }
        };
        walk(walk, root, args, J::object(), J::object(), std::nullopt);
        return ids;
    }
    AuthorWorkflowCompilation compile(const J &root, AuthorBusinessResolver native,
        const J &args = J::object(), const std::string &locale = {}) const {
        std::vector<std::string> active;
        std::size_t count{};
        J used = J::object();
        authoring::SemanticAssets assets(semantic_catalogue_);
        const auto compile_one = [&](auto &&self, const J &source, const J &provided,
                                      const J &extensions, const J &inherited,
                                      const std::optional<std::set<std::string>> &allowed) -> AuthorWorkflowCompilation {
            const auto id = source.at("flow").at("id").get<std::string>();
            Enter enter(active, id, count);
            if (used.contains(id) && used.at(id) != source) authoring::contract_error("FLOW_SNAPSHOT_CONFLICT", id);
            used[id] = source;
            std::string stage = "instantiate";
            try {
            auto concrete = authoring::instantiate_document(source, provided, extensions);
            stage = "semantic_lowering";
            concrete = assets.lower(std::move(concrete), locale);
            stage = "node_compile";
            return compile_author_workflow(concrete, native, {}, [&](const J &call, const J &scope,
                                                                      const std::optional<std::set<std::string>> &nested) {
                authoring::validate_call(call);
                return self(self, lookup(call), call.value("arguments", J::object()),
                            call.value("extensions", J::object()), scope, nested);
            }, inherited, allowed);
            } catch (const nlohmann::json::exception &error) {
                authoring::contract_error("FLOW_JSON_INVALID", id + ":" + stage + ":" + error.what());
            }
        };
        auto result = compile_one(compile_one, root, args, J::object(), J::object(), std::nullopt);
        result.workflow.authoring = {{"schema", 1}, {"format", "public-flow-1"},
            {"root", root.at("flow").at("id")}, {"arguments", args}, {"resource_locale", locale},
            {"documents", used}, {"resources", assets.selections()}, {"source_paths", result.source_paths}};
        return result;
    }
  private:
    struct Enter {
        std::vector<std::string> &active;
        Enter(std::vector<std::string> &a, const std::string &id, std::size_t &count) : active(a) {
            if (active.size() >= 8) authoring::contract_error("FLOW_REFERENCE_DEPTH", id);
            if (std::find(active.begin(), active.end(), id) != active.end()) authoring::contract_error("FLOW_REFERENCE_CYCLE", id);
            // 同一次编译实例数受现有作者图规模控制；并非游戏尝试次数或网络时限。
            if (++count > 1024) authoring::contract_error("FLOW_EXPANSION_LIMIT", id);
            active.push_back(id);
        }
        ~Enter() { active.pop_back(); }
    };
    const J &lookup(const J &call) const {
        const auto id = call.at("flow_id").get<std::string>();
        if (!documents_.contains(id)) authoring::contract_error("FLOW_REFERENCE_MISSING", id);
        const auto &doc = documents_.at(id);
        if (call.contains("expected_revision") && call.at("expected_revision") != doc.value("revision", ""))
            authoring::contract_error("FLOW_REFERENCE_REVISION_CONFLICT", id);
        return doc;
    }
    const J documents_, semantic_catalogue_;
};
} // namespace wvd::games::tasks
