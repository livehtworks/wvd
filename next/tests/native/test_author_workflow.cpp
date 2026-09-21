#include "games/wvd/tasks/author_workflow.hpp"
#include "platform/windows/runtime_files.hpp"
#include "storage/workflow_repository.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using J = nlohmann::json;

J condition(const char *image) {
    return {{"mode", "template"}, {"image", image}, {"threshold", .8},
            {"roi", {0, 0, 900, 1600}}};
}

J workflow() {
    const auto scene = condition("dungFlag");
    return {
        {"schema", 1},
        {"flow", {{"id", "short_flow"}, {"name", "Short flow"}, {"description", "W03"}}},
        {"entry", "recognize"},
        {"nodes",
         J::array({
             {{"id", "recognize"}, {"type", "recognition"}, {"name", "Recognize"},
              {"parameters", {{"condition", scene}}}},
             {{"id", "click"}, {"type", "action"}, {"name", "Click"},
              {"parameters", {{"operation", "click"}, {"scene", scene}, {"target", scene},
                              {"postcondition", scene}, {"postcondition_timeout_ms", 1000}}}},
             {{"id", "wait"}, {"type", "wait"}, {"name", "Wait"},
              {"parameters", {{"duration_ms", 250}}}},
             {{"id", "confirm"}, {"type", "business"}, {"name", "Confirm"},
              {"parameters", {{"binding", "confirm"}, {"operation_id", "author.short"},
                              {"event", "target_completed"}, {"condition", scene},
                              {"expected_step", 0}}}},
             {{"id", "done"}, {"type", "end"}, {"name", "Done"},
              {"parameters", {{"outcome", "success"}}}},
             {{"id", "failed"}, {"type", "end"}, {"name", "Failed"},
              {"parameters", {{"outcome", "failure"}, {"reason", "author.short.failed"}}}}
         })},
        {"edges",
         J::array({
             {{"id", "e1"}, {"from", "recognize"}, {"to", "click"}, {"outcome", "success"}, {"order", 0}},
             {{"id", "e2"}, {"from", "recognize"}, {"to", "failed"}, {"outcome", "failure"}, {"order", 0}},
             {{"id", "e3"}, {"from", "click"}, {"to", "wait"}, {"outcome", "success"}, {"order", 0}},
             {{"id", "e4"}, {"from", "click"}, {"to", "failed"}, {"outcome", "failure"}, {"order", 0}},
             {{"id", "e5"}, {"from", "wait"}, {"to", "confirm"}, {"outcome", "success"}, {"order", 0}},
             {{"id", "e6"}, {"from", "confirm"}, {"to", "done"}, {"outcome", "success"}, {"order", 0}},
             {{"id", "e7"}, {"from", "confirm"}, {"to", "failed"}, {"outcome", "failure"}, {"order", 0}}
         })},
        {"layout",
         {{"nodes", J::array({
              {{"node_id", "recognize"}, {"x", 0}, {"y", 0}},
              {{"node_id", "click"}, {"x", 200}, {"y", 0}},
              {{"node_id", "wait"}, {"x", 400}, {"y", 0}},
              {{"node_id", "confirm"}, {"x", 600}, {"y", 0}},
              {{"node_id", "done"}, {"x", 800}, {"y", -100}},
              {{"node_id", "failed"}, {"x", 800}, {"y", 100}}
          })},
          {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1.0}}}}},
        {"execution", {{"time_limit_ms", 30000}}}
    };
}

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename Function>
void rejects(Function function, const std::string &expected) {
    try {
        function();
    } catch (const std::runtime_error &error) {
        if (std::string(error.what()).starts_with(expected))
            return;
        throw std::runtime_error("unexpected error: " + std::string(error.what()));
    }
    throw std::runtime_error("expected rejection: " + expected);
}

void compiler_case() {
    const auto compiled = wvd::games::tasks::compile_author_workflow(workflow());
    check(compiled.workflow.kind == "author.short_flow", "kind");
    check(compiled.workflow.nodes.at("Entry").at("next") == J::array({"Author_click"}), "success order");
    check(compiled.workflow.nodes.at("Entry").at("on_error") == J::array({"Author_failed"}), "failure order");
    check(compiled.workflow.nodes.at("Author_click").at("custom_action") == "GuardedAction", "action binding");
    check(compiled.workflow.nodes.at("Author_wait").at("custom_action") == "CancelableWait",
          "wait binding");
    check(compiled.workflow.nodes.at("Author_wait").at("custom_action_param").at("duration_ms") == 250,
          "wait duration");
    check(compiled.workflow.nodes.at("Author_confirm").at("custom_action") == "WvdConfirm", "business binding");
    check(compiled.node_to_pipeline.at("done") == std::vector<std::string>{"Terminal"}, "end mapping");
    check(compiled.pipeline_to_node.at("Author_click") == "click", "reverse mapping");
    compiled.workflow.validate();
}

void validation_cases() {
    auto success_only = workflow();
    success_only["edges"].erase(
        std::remove_if(success_only["edges"].begin(), success_only["edges"].end(),
                       [](const J &edge) { return edge.at("outcome") == "failure"; }),
        success_only["edges"].end());
    success_only["nodes"].erase(
        std::remove_if(success_only["nodes"].begin(), success_only["nodes"].end(),
                       [](const J &node) { return node.at("id") == "failed"; }),
        success_only["nodes"].end());
    success_only["layout"]["nodes"].erase(
        std::remove_if(success_only["layout"]["nodes"].begin(),
                       success_only["layout"]["nodes"].end(),
                       [](const J &node) { return node.at("node_id") == "failed"; }),
        success_only["layout"]["nodes"].end());
    wvd::games::tasks::compile_author_workflow(success_only).workflow.validate();

    auto duplicate = workflow();
    duplicate["nodes"].push_back(duplicate["nodes"][0]);
    rejects([&] { wvd::games::tasks::validate_author_workflow(duplicate); },
            "AUTHOR_NODE_DUPLICATE:recognize");

    auto dangling = workflow();
    dangling["edges"][0]["to"] = "missing";
    rejects([&] { wvd::games::tasks::validate_author_workflow(dangling); },
            "AUTHOR_EDGE_DANGLING:e1:to:missing");

    auto no_entry = workflow();
    no_entry["entry"] = "missing";
    rejects([&] { wvd::games::tasks::validate_author_workflow(no_entry); },
            "AUTHOR_ENTRY_UNKNOWN:missing");

    auto unreachable = workflow();
    unreachable["nodes"].push_back({{"id", "orphan"}, {"type", "end"}, {"name", "Orphan"},
                                     {"parameters", {{"outcome", "failure"}, {"reason", "orphan"}}}});
    unreachable["layout"]["nodes"].push_back({{"node_id", "orphan"}, {"x", 0}, {"y", 200}});
    rejects([&] { wvd::games::tasks::validate_author_workflow(unreachable); },
            "AUTHOR_NODE_UNREACHABLE:orphan");

    auto loop = workflow();
    loop["edges"].push_back({{"id", "loop_edge"}, {"from", "confirm"}, {"to", "wait"},
                              {"outcome", "success"}, {"order", 1}});
    rejects([&] { wvd::games::tasks::validate_author_workflow(loop); },
            "AUTHOR_LOOP_UNBOUNDED:");
    for (auto &node : loop["nodes"])
        if (node.at("id") == "wait" || node.at("id") == "confirm")
            node["repeat_limit"] = 3;
    wvd::games::tasks::validate_author_workflow(loop);
}

void repository_case() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("wvd-w03-" + wvd::platform::unique_id());
    try {
        wvd::storage::WorkflowRepository repository(root);
        const auto created = repository.create(workflow());
        check(created.at("revision").get<std::string>().size() == 64, "create revision");
        check(repository.list().size() == 1, "list create");
        check(repository.read("short_flow") == created, "read create");

        const auto copied = repository.copy("short_flow", "short_copy", "Short copy");
        check(copied.at("flow").at("id") == "short_copy", "copy id");
        check(copied.at("nodes") == created.at("nodes"), "copy stable nodes");
        check(repository.list().size() == 2, "list copy");

        auto changed = created;
        changed["layout"]["nodes"][0]["x"] = 12;
        const auto saved = repository.compare_exchange(
            "short_flow", created.at("revision").get<std::string>(), changed);
        check(saved.at("revision") != created.at("revision"), "cas revision");
        rejects([&] {
            repository.compare_exchange("short_flow",
                                        created.at("revision").get<std::string>(), changed);
        }, "WORKFLOW_CONFLICT:short_flow");

        repository.erase("short_copy", copied.at("revision").get<std::string>());
        check(repository.list().size() == 1, "delete");
        rejects([&] { repository.read("../escape"); }, "WORKFLOW_ID_INVALID:");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    check(!error, "cleanup");
}
} // namespace

int main() {
    try {
        compiler_case();
        validation_cases();
        repository_case();
        std::cout << "W03_AUTHOR_WORKFLOW_PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "W03_AUTHOR_WORKFLOW_FAIL: " << error.what() << '\n';
        return 1;
    }
}
