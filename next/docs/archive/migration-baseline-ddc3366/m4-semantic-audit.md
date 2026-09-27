# M4 静态语义审计

本次消费者事实复核仅更新本报告、m4-implementation-map.json、m4-data-mapping.md；不修改
根status、architecture、业务专题报告或任务台账，不派子代理。前次整表审计方法/历史证据保留，
本次重点核对33字段分类、新binding/摘要及旧局部函数是否有活动调用，不重新认领250函数全量验证。
未运行原生测试、构建、设备、网络、commit/push；主代理独占测试。没有import旧模块、
写旧src/config/mod/resources/dist/logs/.vscode，或修改历史M1、m4-task-status.json和专题报告。

## 权威与分母

当前函数/配置的唯一逐项记录是 [m4-implementation-map.json](m4-implementation-map.json)。
每个函数有固定旧定义、已解析词法/同类属性引用、单独标注的动态候选、当前原生引用/摘录、
差异分类与剩余项；每字段另列旧读取/写入/回调、导入保存引用和真实业务取值点。
本报告解释边界，不建立第二份可独立更新的250项状态表。

| 固定来源 | 函数数 |
| --- | ---: |
| src/script.py | 141 |
| src/gui.py | 57 |
| src/main.py | 9 |
| src/utils.py | 31 |
| src/auto_updater.py | 12 |
| 合计 | 250 |

因此“250函数”不是script.py单文件的数量。两个TargetInfo property的getter/setter用来源行号
区分，不按同名符号合并。顶层配置33项；任务固定43 dungeon+15 quest=58。
源码另有fordraig/repelEnemyForces/CaveOfSeperation/steeltrail，保留源码承接，不伪造基础TaskID。

完整任务验收仍 **0/58**、release_allowed=false；M3 Metadata/CLEANUP阻断、
RESOURCE_UNRESOLVED、PERFORMANCE_UNRESOLVED保留。静态覆盖完成不等于语义等价完成，
所以JSON的semantic_review_complete仍false，static_audit.inventory_complete只表示逐项记录齐全。

## 核对方法

以下1-6保留前次整表审计的方法和结果。本次不重新执行Git对象/AST整表审核；仅只读核对
固定旧源、33字段现有消费者摘录及重点调用链，编辑后用JSON结构、分类计数和保护摘要核对
341个ID、验收ID、旧body hash及历史/offline/real状态不变，不据此认领原生测试通过。

1. 只读工作包legacy/src/script.py、gui.py、main.py、utils.py，以UTF-8读取并将换行归一化，
   与Git `6585f4075f5714ab522aa582993860c09af912c1` 对应对象比较。4份一致；
   auto_updater.py在工作包固定副本中不存在，直接只读同一Git对象，不用当前生产源码替代。
2. 用Python标准库AST解析，不执行旧模块。函数采用完整限定名、定义行及结束行；对照原250
   inventory_id，保留原body hash、来源和验收ID。字段从固定33项描述对照旧属性/Subscript/
   getter调用，并记录所在函数、Load/Store/调用参数，配置表出现本身不是消费者。
3. 词法函数引用、self同类属性和构造表达式与动态属性候选分开。每项保留前12条明确引用及总数，
   动态候选前8条及总数单列；候选不是已证明的调用。Python属性协议、日志回调、Tk command/bind
   不能由“没搜到函数名调用”判为无用，也不能由同名属性判为已绑定。
4. 阅读当前原生参数取值、分支、图组合、注册/发布、状态确认、恢复与保存。JSON的current_refs/
   current_business_refs保留实际文件行、符号和代码摘录；audit_chains记录实际消费者/边界。
   单纯字段声明、类型校验、测试字符串、规划owner或类名均不算语义承接。
5. 当前原生引用读取并行工作树；Lovelace的新binding及摘要源码仍待验证。JSON内旧HEAD/hash属于前次整表快照，不能代表本次新增源码。行号可能随主代理编辑移动；
   应按符号/摘录复核，不把工作树hash或最新源码追认成旧测试EXE身份。
6. 前次整表审计已完成JSON结构/唯一ID检查：250+33+58不变，原body hash/验收ID/offline状态不变；当时191个
   唯一原生引用均找到符号/摘录，最终移动的1个定位已刷新，静态源hash写入JSON，不是EXE证据。
   文档diff无空白错误。这不是原生测试，也不读取正在运行的临时结果来推断通过。

## 给主代理的确定缺口

### 无业务读取字段的实际边界

当前7项没有业务读取，不能一律解释为运行能力缺失。33项均已导入/往返；新增AUTO_START_CLASH
和FARM_TARGET_TEXT源码消费者计入有范围读取的26项，但尚未通过本次执行验证。

| 字段 | 当前事实与边界 |
| --- | --- |
| EMU_PATH / EMU_INDEX / ADB_ADRESS | 3项已导入/冻结的旧设备数据，不是新版运行身份；DevicePolicy/LifecycleTarget独立显式绑定。不能为补字段覆盖而推导设备、添加发现或隐式连接 |
| LAST_VERSION / LATEST_VERSION | 2项更新/版本回写尚无新版业务消费者，M6边界；不构成授权退役 |
| WEBSITE_ORG_TIME / AM_REFRESH_TIME | 2项网页提醒/时间回写尚无新版业务消费者，M5边界；原值继续可往返 |

### 已有消费者但不等价完整覆盖

| 字段/旧函数 | 当前实现与未承接范围 |
| --- | --- |
| MAX_TRY_LIMIT | `tasks/dungeon_route.cpp::traverse_dungeon`、`tasks/dark_light.cpp::dark_light` 将冻结值传入 unknown_exhausted；`vision/recognizers.cpp` 以 max_tries<0 或 sample.samples>max_tries 判定，图进入 dungeon.unknown_try_limit。其余map/entry仍各有hit/time预算，不能宣称旧全部重试点参数等价。 |
| MAX_CRASH_LIMIT | `state.cpp::restart_game`、`recovery/boot.cpp::recovery_binding/decide` 已从冻结profile及其binding消费累计上限/升级阈值，不再由入口另传独立max_crashes。新binding一致性检查待验，真实端口未开放。 |
| LANGUAGE | `combat/strategy.cpp::CombatStrategy/uses_task_points` 与 `state.cpp::setting_is` 已按语言解释策略、任务点及重载枚举，属于实际combat消费者。旧gettext日志及Tk/M5本地化未完整迁移，完整配置组合仍待验。 |
| FARM_TARGET | 导入时选区段，新TaskHandoff检验来源/改7000G；m4_check目录遍历/编译不等于从此字段启动生产Farm |
| ACTIVE_BEG_MONEY | 普通 `traverse_dungeon` 和 `dark_light` 均已有 unknown_leap 调用、binding、状态、TaskHandoff/LeapWait源码；暗灯以灯具extra_known排除已知画面。接口摘录已刷新，新调用链和全部任务/恢复/配置矩阵待验，不再记为暗灯无调用。 |
| SaveImage / SaveDebugImage / SaveDebugImageThrottled | 结构化识别/Run结果存在；故障、稀有矿、鱼获PNG持久化与旧按reason节流尚无等价业务消费者 |
| BagClear_Item | 固定旧Factory局部函数仅定义，无调用、传递或注册引用；不计作活动功能丢失。定义/历史记录保留，不授权删除，也不补造新版存仓链；组队和LAST_BAGCLEAR仍不能冒充存仓 |
| GUI/main/日志/更新器 | RunCoordinator真实停止/原子结果不等于旧Tk start/finished队列、配置编辑、日志展示、版本下载/重启链已迁移；旧生产入口保持 |

重点实现引用：`native/storage/legacy_import.cpp::parse`，
`native/games/wvd/state.cpp::restart_game/observe_unknown_leap`，
`tasks/dungeon_route.cpp::traverse_dungeon`，`tasks/dark_light.cpp::dark_light`，
`recovery/boot.cpp::recovery_binding/bind_initial_vpn`，`tasks/task_handoff.cpp::poll`。
具体文件行和调用摘录以JSON为准，以上不是“搜索到名字即证明”的替代证据。

### 已有新代码，不能记成未实现或通过

- AUTO_START_CLASH：已读到Lovelace的profile-aware recovery_binding及bind_initial_vpn，
  将冻结开关/MAX_CRASH_LIMIT装入binding并核对目标授权；TaskHandoff已有初始装配调用。
  源码存在不表示初始VPN、恢复/转交一致性或真实适配已验证，不再写成仅有EnsureVpn枚举。
- FARM_TARGET_TEXT：WvdRunState已发布farm_target_text；settle_legacy_lap及last_lap_seconds
  已承接上一圈结算/摘要源码，未结算为null。last_lap不是新配置字段；不同专项仍按实际旧结算
  边界解释，不能据字段存在认领完整对账或M5/gettext展示通过。

- Repel/Fordraig/COS/Steel均有源码。最新静态复核已见Fordraig/COS的CLI编译分支、CMake注册、
  WvdRunState确认事件和摘要；多阶段发布及fixture接线已加入。本次不推断其它代理的当前构建/运行结果，不标PASS。
- `publish_workflow_stages` 已有实现，将各stage节点命名空间化，发布为一个bundle revision；
  每Session独立entry/checkpoint/time/dialogue binding。离线fixture已调用该发布入口，最终装配需由当前构建/验证确认；
  保留runtime同revision限制，不能为接线绕开冻结身份。
- 普通路线的unknown_leap经PipelineCompiler、发布器WvdUnknownLeap binding、state_factory注册、
  同帧观测到WvdRunState意图已能静态追踪。TaskHandoff::poll检查旧Run真静止、结果提交和来源后
  才编译/启动7000G；LeapWait按原起点分段计7300秒。新链的实际触发/停止/存储失败/未确认
  副作用/完整任务覆盖仍未验证。单类/枚举存在不销项，已接线也不再倒写为完全缺实现。
- STRATEGY、任务点推进、默认/专项对话、住宿付款、角色恢复、复活、未知窗、善恶CAS等已存在，
  剩余是任务/恢复/配置/mod矩阵，不应继续沿用早期“只解析数据”“全部未接线”的文本。
- `freq_var`在固定旧StateCombat没有执行读取，新版保留不发明频次；`_SUICIDE`的旧置位
  不等于存在自杀动作消费者。两者不能借迁移擅自补出业务行为。

## 验证失败不等于没有实现

分项结论来自本地执行证据，不是用户提供的外部结果；汇总入口为
[m4-business-validation.md](../m4-business-validation.md)，具体轮次、输入数、失败原因及
EXE身份/真静止证据以各专题当前报告为准，本表不复制会随复验过期的计数：

- [7000G](../m4-gold-income-validation.md)、[Golden](../m4-golden-chest-validation.md)、[牛洞](../m4-bull-cave-validation.md)。
- [沙人](../m4-sandman-validation.md)、[钢试炼](../m4-steel-trial-validation.md)。
- [默认对话](../m4-default-dialogue-validation.md)、[时间跳跃](../m4-time-leap-validation.md)；因果链分项见业务汇总及对应任务专题。

夹具自检、发布校验、业务输入/后置失败及UserStopped分别解释，不能合并成全通过。
局部分项PASS不解除其他完整流程的阻断，也不改变完整任务验收 **0/58**。

## 维护约束

本轮不修改历史M1或m4-task-status.json。m4_inventory.py的早期生成逻辑和
m4-semantic-differences.json的历史remaining中仍可能有过期“未接线”用语；本次无权编辑它们，
不得用这些旧remaining覆盖本轮current_refs。历史差异/批准依据仍保留，当前实现状态以本表为准。
以后重生成叠加表须保留并复核semantic_audit，不能机械刷新旧NOT_STARTED或用运行计数填假通过。
不改正式配置/资源/数据库，不增双读双写，不切生产，不进入M5。
