#pragma once

#include "pipeline_compiler.hpp"
#include "workflow/program.hpp"

namespace wvd::games::tasks {
nlohmann::json audit_event_responsibilities(const CompiledWorkflow &source,
                                           const workflow::FlowProgram &program,
                                           bool collect_report = true);
workflow::FlowProgram compile_native_program(const CompiledWorkflow &source,
                                             const nlohmann::json &source_paths,
                                             const std::string &revision);
} // namespace wvd::games::tasks
