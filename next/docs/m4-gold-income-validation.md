# M4 7000G 剧情链

固定旧源`6585f407`，十阶段正常剧情两分支已在实际Maa通过，完整任务矩阵尚未通过。

- `tasks/gold_income`编译十个明确阶段；`quests/GoldIncomeCycle`由WvdRunState独占。
  先FortressArrival跳跃、返回要塞、进入王城，再接取、旧区、王城、三个人物、拒绝、答应任务。
- 三个固定人物位置保持`[450,1111]`、`[200,1180]`、`[680,1200]`，含why选项和第三人
  leavethechild；旧区可直接出现或先出现iminhungry。保留15/4/8秒剧情等待。
- 剧情子图只在已知选项/fastforward/城市锚点下输入；未知页不会沿旧版无限点`[1,1]`。
  这项安全收窄需在实际离线场景矩阵中保留失败边界，不宣称未知真实剧情均可处理。
- 每阶段操作前记录pending，新帧阶段终点才清除；自动恢复不能重放未确认阶段。
  幂等ID包含固定阶段操作名和周期号，不使用推进后变化的phase重新生成回执键。
- `estimated_income`仅在最后noeasytask后真实识别ruins才按7000递增，明确
  `income_is_estimate=true`，不是账号余额或真实奖励到账证明。

状态两周期/20阶段/恢复意图/重复回执已有分项证据；23/24输入完整剧情两分支及
首个跳跃停止/拒绝已通过。待验未知后置、配置/mod及任务交接；没有从旧Python运行游戏逻辑。

## 实测与定点修正

状态35方法组通过；计划首轮因15秒单节点延迟超过10秒契约而失败，
已拆为10秒后再新帧确认等待5秒，未更改总等待或通用上限。
重建后单任务资源图可编译。流程首轮`m4-workflow-4o72cdln/gold-income-full-False`
Interrupted/10输入，没有命令不匹配，phase=5、pending=false、估算收益0。
事件显示Confirmed4之后进入RecoveryRequired：十阶段共用Stage的默认max_hit=5，
并非实际达到1500秒上限。修正Stage为十次后重新构建并执行如下验证。
同轮停止/拒绝各1输入、pending保留；不能以负例通过替代完整正例。

`m4-workflow-7g_0bqno`（`m4-specials-correction-workflow.log`）两条完整剧情
分别Completed/23与24输入；停止UserStopped/1、拒绝Failed/1且不继续剧情。
四场景已核对当前测试EXE哈希、准确输入、无错序及真正静止，不改写首轮失败结果。
同产物M2 104方法通过；该轮其它专项失败不能改为整组通过。
