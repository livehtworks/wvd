#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include <array>

namespace wvd::games::navigation {
// 对应旧 CursedWheelTimeLeap 的 CSC_symbol=None 调用。不用于启用因果调整的任务。
// 本层只编译有限视觉/输入图；跳跃次数、任务阶段由调用者的业务状态持有。
tasks::CompiledWorkflow time_leap_without_causality(const std::string &target,
    const std::string &chapter = "cursedwheel_impregnableFortress", bool allow_download = true);
struct CausalityOption {
    std::string image;
    std::array<double, 3> rgb;
};
struct CausalitySettings {
    std::string symbol;
    std::vector<CausalityOption> options;
};
// 保留旧可见目标快路径：快路径成功时不调整因果；仅慢路径跳跃前应用指定设置。
tasks::CompiledWorkflow time_leap_with_causality(const std::string &target, const CausalitySettings &settings,
    const std::string &chapter = "cursedwheel_impregnableFortress", bool allow_download = true);
}
