#pragma once
#include "pipeline_compiler.hpp"
#include "public_flow_library.hpp"

namespace wvd::games::tasks {
enum class BountyVisit { Reveal, Report };
CompiledWorkflow leave_bounty_board(const PublicFlowLibrary &, const std::string &locale);
CompiledWorkflow visit_bounty_board(BountyVisit, const PublicFlowLibrary &,
                                    const nlohmann::json &root, const std::string &locale);
}
