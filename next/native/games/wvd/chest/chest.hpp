#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include <cstdint>

namespace wvd::games::chest {
// seed 由运行请求提供并随编译图封存，测试无需替换随机/视觉实现。
tasks::CompiledWorkflow open_chest(int preferred_character, bool quick, std::uint32_t seed);
}
