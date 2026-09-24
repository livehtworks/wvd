#pragma once

#include "pipeline_compiler.hpp"
#include "workflow/program.hpp"

namespace wvd::games::tasks {
workflow::FlowProgram compile_native_program(const CompiledWorkflow &source,
                                             const nlohmann::json &source_paths,
                                             const std::string &revision);
} // namespace wvd::games::tasks
