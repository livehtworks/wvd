#include "games/wvd/tasks/author_workflow.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using J = nlohmann::json;
        J document{{"schema", 1},
            {"flow", {{"id", "slot-six"}, {"name", "Slot six"}, {"description", ""}}},
            {"entry", "Slot"},
            {"nodes", J::array({
                J{{"id", "Slot"}, {"type", "slot"}, {"name", "Slot"},
                  {"parameters", {{"name", "extra"}, {"calls", J::array({
                      J{{"flow_id", "shared"}}, J{{"flow_id", "shared"}}})}}},
                  {"repeat_limit", 6}},
                J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
                  {"parameters", {{"outcome", "success"}}}}})},
            {"edges", J::array({J{{"id", "slot_done"}, {"from", "Slot"},
                {"to", "Done"}, {"outcome", "success"}, {"order", 0}}})},
            {"layout", {{"nodes", J::array({J{{"node_id", "Slot"}, {"x", 40}, {"y", 100}},
                J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}})},
                {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
            {"execution", {{"time_limit_ms", 30000}}}};
        int resolutions = 0;
        const auto compiled = wvd::games::tasks::compile_author_workflow(
            document, {}, [&](const J &) {
                ++resolutions;
                wvd::games::tasks::PipelineCompiler child("test.shared");
                child.route("Entry", {"Terminal"});
                return wvd::games::tasks::AuthorWorkflowCompilation{child.finish()};
            });
        auto program = wvd::games::tasks::compile_native_program(
            compiled.workflow, compiled.source_paths, "slot-six-native");
        program.validate();
        const auto &root = program.definitions.at(program.root_definition);
        if (resolutions != 2 || root.steps.at("Author_Slot").max_hit != 6 ||
            root.steps.at("Author_Slot_Call0").max_hit != 6 ||
            root.steps.at("Author_Slot_Call1").max_hit != 6 ||
            program.definitions.size() != 3)
            throw std::runtime_error("NATIVE_SLOT_CALL_SCOPE_INVALID");
        const auto &first = std::get<wvd::workflow::Call>(
            root.steps.at("Author_Slot_Call0").data);
        const auto &second = std::get<wvd::workflow::Call>(
            root.steps.at("Author_Slot_Call1").data);
        if (first.definition == second.definition ||
            !program.definitions.contains(first.definition) ||
            !program.definitions.contains(second.definition))
            throw std::runtime_error("NATIVE_SLOT_CALL_SHARED_COUNTERS");
        std::cout << "native author slot six and independent calls passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
