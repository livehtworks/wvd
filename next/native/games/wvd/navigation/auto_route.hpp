#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::navigation {
// 自动搜索按无目标提示或提交后的到达状态结束；单纯停止只交回外层。
// handoff_destination让目标任务接收明确的导航结束，不把它当成战斗胜利。
tasks::CompiledWorkflow auto_route(const std::string &target, bool handoff_destination = false);
}
