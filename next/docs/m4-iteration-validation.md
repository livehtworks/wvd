# M4 正常 Farm 迭代

## 已接入链路

`tasks::dungeon_iteration` 将正常 Farm 的一次外层迭代编译成同一有限 Maa 图：

```text
当前画面
  本内 -> StateDungeon 路线
  本外 -> 回城/补给 -> EOT 前结算上一轮 -> 入本 -> StateDungeon 路线
  路线正常返回 -> 本段检查点 -> RunCoordinator 的下一正常段
```

子流程不是新 Run，普通续段沿用已有 RunCoordinator。代次变化使旧观察失效，业务状态/策略由同一 Run 持有；EOT 前才结算上一轮，本内启动不伪造已经离开过副本。

路线终点按原参数保留，不能把所有任务改成最后回城。有些路线在坐标点结束，有些经哈肯或自动寻路退出。一段正常结束不等于整项 TaskID 的全部语义已迁移。

地图/自动寻路现用新画面的退场证据识别 returnText、returntoTown、openworldmap、worldmapflag 等出口；不再只给哈肯名字的目标允许退场。坐标目标直接出本也停止旧移动输入，由父图先判断 Outside，不假记坐标已到达。地图与战斗反证仍保留。

本组合段总预算为 640 秒（已有补给 180、入本 60、路线 400 的有限总范围），子调用不重启父总时钟；不是宣称旧按进展/遭遇计时的冻结策略已完全等价。没有延长输入帧有效期或停止预算。

## 实际验证

构建 `m4-iteration-build.log` 成功。

- `m4-iteration-plan-tests.log`：6 方法通过，18.003 秒；43 项原 TaskID 的补给/入本/路线组合图、RTT 和逐任务点绑定、资源引用核对通过。证据 `next/.local/m4-plan-12i_aq6w/`。
- `m4-iteration-tests.log`：5 方法/8 场景通过，277.132 秒；证据 `next/.local/m4-workflow-rtoehnkv/`。
- 正常城内起步到出本，4 次输入；两段连续迭代共 10 次输入、2 个正常代次、2 场战斗，第二段 EOT 前结算上一轮得到 dungeons=1，新的入本供给周期为 2。
- 坐标目标三种直接退场各只发两次导航输入，不推进目标计数；自动撤退能结束于返回提示。
- 输入拒绝或 Stop 后已完成段数为零，没有启动第二段。

真实固定 Maa、合成模板、动作因果离线设备；没有按截图次数推进，也没有使用假状态工厂替代业务。执行前后核对 EXE hash，记录实际 SDK DLL 来源/hash。

同构建导航回归 12 方法通过（252.729 秒，m4-iteration-navigation-regression.log）；状态回归 11 方法通过（3.821 秒）；核心停止/恢复/子任务/根终态/保存失败/会话预算定点回归 15 方法通过（12.752 秒，m4-iteration-runtime-regression.log）。本阶段没有声称重跑全部核心 100 项；上阶段完整核心记录保留在 m4-departure-validation.md。450 个旧文件哈希核对无变化。

## 仍未完成

`NORMAL_FARM_ITERATION_NOT_FULL_TASK` 只表示正常迭代图。全局启动阻塞/死亡/复活/Pause/对话、完整任务恢复、15 项专项、确认与保存之间的对账仍未齐；43 项静态编译不是 43 次完整任务执行，全部任务执行验收仍为 0/58。

M3 发现链阻断、RESOURCE_UNRESOLVED、PERFORMANCE_UNRESOLVED、真实视觉质量和任意原生等待的取消边界保持。不操作真实设备，不切换生产，不进入 M5。
