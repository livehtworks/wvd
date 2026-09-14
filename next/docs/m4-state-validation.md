# M4 运行状态与有限段验证

日期：2026-09-14。修改基线 `4d7c4fa`；旧行为核对基线 `6585f407`。
状态实现已落地，M4 整体仍为 `M4_PARTIAL_IMPLEMENTATION`，58 项完整任务执行通过数仍为 0。

## 分层与所有权

- `contracts/business_state.hpp` 只定义状态、单调时钟和段边界，不认识 WVD。
- `BehaviorRegistry` 封存非捕获状态工厂；`RunDefinition` 冻结工厂身份、参数、段顺序及预算。
- `RunCoordinator` 独占一个 Run 的状态；新 Run 重新创建，Session 只借用。
- `Context::with_business_state` 在取消检查和状态锁内调用业务，不能假设 Maa 回调总在同一线程。
- `WvdRunState` 管计数、任务步与旧重置事件；`CombatStrategy` 管策略来源及行消费，不截图或点击。
- 观察者只读段静止后的 JSON 摘要；运行时不重读可编辑 profile，不向观察端暴露可变指针。

正常续段必须同时满足前段 Completed、检查点属于同一根 task_id/代次/深度、Session 真静止。
协调器才开启冻结列表的下一段，并增加 generation。子流程完成不能单独续段。
恢复另走 `RecoveryRequired` 和纯决策；进入恢复段本身不等于游戏已经重启。
停止与创建下一段工作线程串行确定先后；max_business_units 为 1..256，各段另有时间预算。
没有第二套单节点调度器，也没有无限 Farm 循环。
状态工厂沿用注册表每个 binding 参数 32 KiB 上限，超限明确拒绝；配置副本能导入不等于任意规模策略都已通过运行绑定。

## 策略兼容

固定旧源 ReloadStrategy（1756 行）、StateCombat（2502 行起）及任务步/地下城事件是语义依据。
默认/任务专用/任务点选择、首个同名分组、深复制、空策略自动战斗、中英业务键均保留。
角色按原行顺序和 `role / role_sp / role_alt` 比较，严格更高分才替换，最低 0.80。
接口复核修正了新策略实现的一处边界：相关分数 -1..0 是合法未命中，不作为后端 Error。
NaN 和超出相关系数范围的值仍拒绝；新增负分用例，不降低成功阈值。
目标失败、取消、未确认的自动兜底不消费。复核固定旧源后修正此前过宽的说明：已选择条目并确认自动兜底成功时也消费该条目；未匹配角色的临时 Auto 不消费。技能成功或已确认兜底删除对应行，“释放任一即完成”才清空余行。实际有限施法证据见 `m4-combat-turn-validation.md`。
选择令牌绑定 Run、generation、策略 epoch 和原行内容，重复或跨代次确认拒绝。

freq_var 保留原值：固定旧战斗实现没有读取它，不能凭字段名增加频率规则。
“每场战斗前”实际在检测过 Combat 后返回 Dungeon 时重置，不在每个 Combat 帧重置。
重启/复活重置策略；正常段续接不补满，入本/任务点变化依旧配置触发。

## RuntimeContext 生命周期

下表覆盖固定旧声明 92..124 行。待业务接入不等于退役旧功能；旧版仍为唯一生产入口。
“无有效读取”只记录核对结果，不据此删除生产代码或发明新消费者。

| 原字段 | 新归属及正常段/重启行为 | 当前边界 |
| --- | --- | --- |
| `_RUNNING_EMU_PID` | 设备绑定与连接代次，不进入跨段业务状态 | 发现链仍阻断；不保存 PID 自动恢复 |
| `_LAPTIME` | lap_started_，跨段保留，新 Run 空；重启不清 | 已实现 |
| `_TOTALTIME` | total_seconds_，按旧 lap 结算，区别整个 Run elapsed | 已实现 |
| `_COUNTERDUNG` | dungeons_，有战斗/宝箱经历才计一次 | 状态 API 已实现，任务出口未接入 |
| `_COUNTERCOMBAT` | combats_，回到 Dungeon 才结算 pending | 状态 API 已实现 |
| `_COUNTERCHEST` | chests_，重复观察不重复计数 | 状态 API 已实现 |
| `_TIME_COMBAT` | 可空单调起点；跨正常段保留，restart_game 清除 | 已实现 |
| `_TIME_COMBAT_TOTAL` | 累计战斗秒数，重启不清 | 已实现 |
| `_TIME_CHEST` | 可空单调起点；跨正常段保留，restart_game 清除 | 已实现 |
| `_TIME_CHEST_TOTAL` | 累计宝箱秒数，重启不清 | 已实现 |
| `_MEET_CHEST_OR_COMBAT` | met_encounter_；完成一轮结算后清除 | 已实现 |
| `_COMBATSPD` | 旧源只有赋值；保留默认与重启清除事实 | 倍速动作待 M4.5 |
| `_SUICIDE` | 旧源置位、复活清除，无有效读取 | 死亡动作待 M4.5，不新增空消费者 |
| `_MAXRETRYLIMIT` | 旧固定值无读取；新版会话/恢复预算另有权威 | 不擅自翻译成业务重试次数 |
| `_ACTIVESPELLSEQUENCE` | 固定源仅声明，实际消费来自 CURRENT_STRATEGY | 不创建第二套技能状态 |
| `_RECOVERAFTERREZ` | recover_after_rez_ 由 resurrected 置位，正常段保留 | 恢复动作和标志消费待 M4.4/5 |
| `_ZOOMWORLDMAP` | 默认 false，restart_game 清除，正常段不改 | 缩放成功置位待导航动作 |
| `_CRASHCOUNTER` | crashes_，旧超出 MAX_CRASH_LIMIT 归零规则 | 只实现计数，不执行模拟器重启 |
| `_IMPORTANTINFO` | 后续由结构化诊断派生，不搬成无限字符串 | 完整业务消息待 M4.8 |
| `_RESUMEAVAILABLE` | 旧进 EoT 清、入地图设、移动失败清 | 导航动作待 M4.4，未伪造可用状态 |
| `_BYPASSAFTERRESTART` | 默认 true，restart_game false，正常段不改 | 绕墙成功置回待 M4.4 |
| `CURRENT_STRATEGY` | CombatStrategy 深复制并消费，旧事件重置 | 已实现，成功输入判据待战斗接入 |
| `NEED_RECOVER_WHEN_BEGINNING` | 入本置位，正常段保留 | 补给消费待 M4.4，不每段重置 |
| `TASK_STEP_INDEX` | task_step_，目标完成递增，入本归零 | 状态 API 已实现 |
| `_LAST_BAGCLEAR` | 按已结算总秒数的六小时整数区间 | 状态 API 已实现，不自动清用户背包 |
| `_SKIP_SCREENSHOT_WARNING` | 截图连接诊断节流，不属业务事实 | 对应设备诊断，不跨重建保存 |
| `_LAST_FOCUS_CHECK` | 当前连接前台观测节流 | 不把旧前台证据带入新 Session |
| `_PAUSE_CHECK_UNTIL` | 启动恢复后的短期检测窗口 | 待 M4.6，用单调期限而非长期暂停状态 |
| `_LAST_DEBUG_IMAGE_AT` | 有界诊断节流，不作完成依据 | 待 M4.8，不从旧截图续跑 |

计时使用注入的 steady_clock，0 时刻也是合法开始。战斗和宝箱重叠时保留旧
“较长者减去较短者”口径，这是兼容统计，不是证明精确游戏耗时。
elapsed_seconds 为整个 Run 时间，total_seconds 为旧 lap 累计，两者不能混报。
pending 战斗/宝箱事实用于返回 Dungeon 的结算；重启后的业务进度恢复仍须 M4.6 对齐。

## 本轮验证

| 验收 | 实际证据与结论 |
| --- | --- |
| 原生构建 | 状态/计划实现过程构建成功，负分边界修正后的最终构建 m4-state-final-build |
| 数据回归 | 8 组通过，m4-data-9k5_p23l；33 字段和 58 项目录完整 |
| B-STATE | 6 组通过，m4-state-tests-18qm7y86；真实 C++ 状态/协调器、真实 Maa 离线 Controller |
| 状态直接断言 | 阈值/别名、失败不消费、重复/旧代次/跨 Run 拒绝、计时、中英任务点、复活/返回 Dungeon 重置 |
| 实际有限段 | 两段续接、新 Run 隔离、冻结后调用者修改无效、缺失/子检查点拒绝、停止不续段、恢复与续段分开 |
| M2 回归 | 状态及计划版本均通过 100 项；最终构建结果见 m2-final-2.log |
| M3 定点回归 | ROI、Custom 因果门禁、活动资源完整性 3 组通过；最终构建结果见 m3-final-2.log |

统一日志根：`next/.local/m4-state-31d6be223ee5454daa24b726aad9c20c/`。
所有场景写入测试专属目录，使用合成帧和离线 DeviceBackend，真实连接及输入为零。
状态测试动作只调用状态 API，不伪装成技能/寻路实现，不证明真实 NEXT/Pause 质量。
最终构建身份为 `96c9b74c48c61c39ffc1b250b78acbb7bcbfa2bdc8e31c08952e4f6a39f0e5c5`。
M4 新入口尚未逐进程枚举加载 DLL；审核包记录固定依赖/EXE/source hash，不冒充完整 DLL 加载证据。
M3 metadata 已知失败未重复运行挑 PASS，固定性能窗口和 M0 资源窗口未重跑。

## 剩余

状态消费者、任务计划/Pipeline、导航补给、战斗宝箱、业务恢复、15 个专项任务和写回诊断继续按 M4 执行。
M3 发现收尾、完整性矩阵、性能/资源未决保留；M5、真实设备、旧打包、提交推送不在本轮许可内。
上一轮审核 ZIP 不覆盖本次实现，不能拿旧源码哈希为当前状态背书。
