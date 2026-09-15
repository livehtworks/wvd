# M4 半自动大恶魔专项

## 实现范围

固定旧源码`QuestFarm::manualSepDemon`：stair_2/哈肯退出、两次返回、真实住宿、
DHI/BeautifulOre跳跃、COS/COSB2F入口、stair_3/固定坐标。源任务树保持不变。
哈肯属于离本目标，独立受控子图只有选中并自动移动后才确认离本；不能把提前回城
当作哈肯到达。城内启动按旧StateDungeon直接返回的语义进入返回/住宿阶段。

`quests/ManualSeparation`只保存阶段和未确认副作用；`WvdRunState`拥有生命周期。
`tasks/manual_separation`编译Maa图，不另写执行循环。`configure_manual_separation_units`
将同一封存Session定义配置为恰好两段，继续由RunCoordinator在真正静止后推进。
第一段完成不等于整任务完成。两次返回和跳跃都先记意图，未确认时恢复不得盲目重放；
住宿沿用付费待确认和已付款回执。第二段完成只记一次性完成，不增加普通副本次数。

两段各自包含一条有限路线及360秒补给/跳跃余量；恢复包装的120秒另计，仍受30分钟
Session上限。它不是把两条路线放进一个无限Session，也不更改核心256段上限。
离线检查器明确导出required_normal_units=2。无设备入口或旧版切换。

## 待验证

`9cdbd70`构建通过；状态25方法通过（7.977秒，`m4-manual-state.log`），计划8方法
通过（31.288秒，`m4-manual-plan.log`）。正式资源清单覆盖两段全部依赖。

首轮真实流程三方法均失败（120.584秒，`m4-manual-workflow.log`、`m4-workflow-z2pim89c`）：
完整链1输入、提前离本及拒绝场景0输入，均为SCENE_UNCONFIRMED；完整链场景帧龄
约2023ms。拒绝场景在预期输入前已失败，不算拒绝/停止语义通过。

修正一：地图路由启用已经验证的基础阻塞分片；同步并行识别结束后仅合并本次调用的
模板叶节点memo，避免后续反证重复匹配。复合模式/时序状态不合并，不改变ROI/阈值/TTL。
补恢复决策的未确认返回/跳跃拒绝，避免为了重入任务而反复重启。
待复验，没有把编译图数量当作完整任务通过，58项分母不变。

修正一产物`bed7833`：完整链与提前离本各执行三次正确输入后仍SCENE_UNCONFIRMED，
失败发生在AutoMove后置（场景约2526ms），没有越过门禁继续发第四次输入。
修正二将map|moving|encounter|outside的后置条件等价收敛为明确锚点并集，保持允许地图、
不额外允许RiseAgain/无目标提示。只在动作后分类，不替代下一次输入的场景门禁。
这是同一流程帧龄问题最后一次定点修正，旧失败证据继续保留。

修正一整组三方法一通过、两失败，183.260秒。停止/拒绝两场景分别UserStopped/Failed，
各1输入且无错点，已在重建前核对EXE身份与真正静止；这两例不代表完整路线通过。

修正二产物`74b99fb`：三方法均失败（203.838秒，`m4-manual-correction2-workflow.log`、
`m4-workflow-8qtzoenz`）。完整链五输入后BUSINESS_CONFIRMATION_STALE，提前退出一输入后
SCENE_UNCONFIRMED，拒绝场景零输入CUSTOM_ACTION_FAILED；都未到达整任务终点。
已达到本包该问题两次定点修正边界，保留BLOCKED，不再复跑挑PASS，也不延长TTL。
同产物M2 104方法通过（96.477秒），移动/地图后置谓词两方法通过（50.121秒）；
这不能替代完整任务失败结果。
