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

已接原生库、离线检查器与现有Maa流程夹具；待构建、状态契约、清单绑定及真实离线
双段/停止/拒绝/提前回城验证。没有把编译图数量当作完整任务通过，58项分母不变。
