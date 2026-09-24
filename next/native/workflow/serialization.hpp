#pragma once
#include "program.hpp"

namespace wvd::workflow {
nlohmann::json serialize(const FlowProgram &program);
} // namespace wvd::workflow
