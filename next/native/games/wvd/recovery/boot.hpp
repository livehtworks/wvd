#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow wait_boot_ready(bool allow_download);
// 同代次普通插入：先消除已知阻塞层，再证明已回到游戏场景；不调用生命周期端口。
tasks::CompiledWorkflow clear_common_screens(bool allow_download, DialoguePolicy policy = DialoguePolicy::Default);
// 恢复图只负责到达已知场景，然后回到原任务入口重新观察，不续用原节点坐标。
tasks::CompiledWorkflow with_boot_recovery(const tasks::CompiledWorkflow &task, bool allow_download);
}
