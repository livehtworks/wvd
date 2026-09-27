#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include <stdexcept>

namespace wvd::games::combat {
// 只构造选级业务步骤；不截图、不直接输入、不消费策略、不重新分类副本/宝箱。
// 等级步骤的组合逻辑只有作者仓库一份；这里只装配实例参数和调用者结果端口。
inline nlohmann::json append_level_selection_steps(tasks::PipelineCompiler &graph,
    const std::string &prefix, int level, const nlohmann::json &casting,
    const nlohmann::json &next, const nlohmann::json &fallback) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (prefix.empty() || level < 1 || level > 9 || !casting.is_object() ||
        !next.is_array() || next.empty() || !fallback.is_array() || fallback.empty())
        throw std::runtime_error("COMBAT_LEVEL_STEP_CONTRACT_INVALID");
    graph.public_step(prefix + "SelectLevel", "combat-select-level", {{"level", level}}, next,
                      {{"unavailable", level == 1 ? next : fallback}});
    return J{prefix + "SelectLevel"};
}
} // namespace wvd::games::combat
