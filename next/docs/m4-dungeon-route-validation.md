# M4 地下城路线组合

## 当前范围

`tasks/dungeon_route.cpp::traverse_dungeon` 将已有地图/自动寻路、有限遭遇战、宝箱和角色恢复接到同一个 Maa 图。不是另建调度器；策略与 task_step 仍由本 Run 的 WvdRunState 持有，普通插入不创建新代次。

- 根据原 route 顺序分派任务点；子图到达后，WvdConfirm 用新帧再次确认才推进 task_step。
- 遇战斗/宝箱返回重新分派，不提前推进任务点；战斗结束后按原设置决定角色恢复，再继续同一个点。
- 宝箱转战斗/复活是显式普通返回边，父图重新观察。通用 `RecoveryRequired` 不允许被 define_child 当正常返回吞掉。
- 已在城内或出现退出提示时离开 StateDungeon，不发送剩余地图点；退出不虚记最后一个地图目标完成。复活仍保留明确的恢复需求出口，未宣称复活完整处理已接齐。
- 保留旧地图/地下城分支区别：已打开地图时不新增关闭地图并立即恢复的动作。

每个子作用域重置其自身节点预算，不重置父图、策略、任务点、帧代次或输入许可。完整入本/回城/住宿循环、其余对话/死亡/Pause/异常恢复以及 15 项专项尚未全接齐，完整任务依旧 0/58。

## 有限时间预算

编译产物新增显式 `time_limit`，发布到既有 SessionDefinition 并进入版本身份和运行结果；不是测试专用配置。默认短子流程仍为 60 秒，地下城组合段为 400 秒，独立启动确认 120 秒；带启动恢复的组合保留原业务预算并另计 120 秒。发布图预算须正值且不超过 30 分钟，超过则拒绝，不截断或静默重置。

400 秒参考旧无进展检测窗口，用作本有限组合段的总上限，**不等同于已迁移旧版所有按战斗/宝箱计时的冻结诊断**。更长业务仍需按任务的正常有限段续接，不能靠放大帧有效期获得输入许可。帧有效期保持 2 秒，stop_timeout 不变。子图被内联或原生调用时沿用父 Session 预算，不各自重启总计时。

原生夹具等待最终结果依据实际 RunDefinition 的 Session 数和预算；Python 外部 watchdog 仅是更外层测试期限，不是生产取消方案。

## 已有证据

首组 `m4-dungeon-route-tests.log`：4 方法/5 场景通过，100.596 秒，证据 `next/.local/m4-workflow-ndboak8g/`。首次目录验证 `m4-dungeon-route-plan-tests.log` 的 5 方法通过，覆盖原 58 项计划数据、43 项入本和新增 43 项路线图参数/资源；这组早于最终预算元数据构建，保留原身份。

最终构建 `m4-dungeon-route-budget-build.log` 后：

- `m4-dungeon-route-final-tests.log`：11 方法通过，227.913 秒，`next/.local/m4-workflow-zgdc0o_5/`。实际包含 8 个路线场景、原生子流程/边界负例以及两种非法 Session 预算。
- 战斗打断后完成两个任务点，3 次输入、1 场战斗；宝箱转战斗后 4 次输入，同时结算 1 箱/1 战。
- 战后完整恢复再导航，6 次输入；确认 healing_sequence=1、healing_required=false、task_step=1，运行定义保存 time_limit_ms=400000。
- 退出提示停止地图输入；城内/复活页不发送地图输入；输入拒绝不推进目标；已开地图不提前打开恢复面板。
- 同构建 `m4-dungeon-route-plan-regression.log` 5 方法通过（5.643 秒），目录证据 `next/.local/m4-plan-gv_t02rx/`；状态组 11 方法通过（3.870 秒）。

每个工作流保存执行前后核对的 EXE hash 和实际加载 SDK DLL 来源/hash；台账工具同时核对路线和目录两种 EXE 身份。43 条 `route_compilation` 只填写静态编译/资源通过，executed=false，完整任务 offline_status 仍 NOT_RUN；没有把通用合成场景推广为逐任务完整执行。

同构建补验：M2 100 方法通过（110.953 秒，m4-dungeon-route-m2-regression.log）；M3 三个允许的 ROI/Custom/组合条件方法通过（30.933 秒，m4-dungeon-route-m3-regression.log）；启动恢复 11 方法通过（167.202 秒，m4-dungeon-route-recovery-regression.log）；地图 5 方法通过（57.113 秒，m4-dungeon-route-map-regression.log）。450 个旧文件 SHA256 核对无变化。

M3 发现链阻断、RESOURCE_UNRESOLVED、PERFORMANCE_UNRESOLVED、真实 NEXT/Pause 与原生取消边界均保持。不用上述定点回归代替整个工作包或全部 58 项任务的验收。
