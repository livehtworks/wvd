#pragma once
#include "pipeline_compiler.hpp"

namespace wvd::games::tasks {
enum class BountyVisit { Reveal, Report };
CompiledWorkflow visit_bounty_board(BountyVisit);
}
