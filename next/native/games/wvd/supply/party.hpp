#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include <optional>

namespace wvd::games::supply {
tasks::CompiledWorkflow assemble_party(const std::optional<std::string> &party_image = {});
tasks::CompiledWorkflow assemble_and_rest(bool royal_suite,
                                         const std::optional<std::string> &party_image = {});
}
