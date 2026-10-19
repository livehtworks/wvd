#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::recovery {
inline void preserve_low_willpower_stop(tasks::PipelineCompiler &graph) {
    // Same direct stop for entry and startup; no handler, input or replan.
    graph.default_body_event(nlohmann::json{{"id", "wvd-low-willpower"}, {"class", "exception"},
        {"priority", 1000}, {"source_node", "Entry"},
        {"detect", vision::low_willpower_warning()}, {"disposition", "external_blocked"},
        {"reason", "party.low_willpower_manual_required"}});
}
}
