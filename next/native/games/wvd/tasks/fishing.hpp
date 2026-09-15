#pragma once
#include "pipeline_compiler.hpp"

namespace wvd::games::tasks {
// 抛竿子链只确认进入等待咬钩页，不代表钓到鱼、完成补饵或整项任务。
CompiledWorkflow cast_fishing_line(bool far);
CompiledWorkflow collect_fishing_reward();
// 一次有限钓鱼结果：已确认收获或超过300秒后的已确认收竿失败，不含补饵导航。
CompiledWorkflow fishing_round(bool far, bool allow_download = true);
CompiledWorkflow seek_fishing_position();
}
