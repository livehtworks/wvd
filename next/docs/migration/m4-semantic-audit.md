# M4 静态语义审计

本次职责：工作包section20的确定性清单/文档分工。只写本报告、m4-implementation-map.json、
m4-data-mapping.md、architecture.md、m4-business-validation.md；不派子代理。
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
5. 当前原生引用来自本轮工作树，包含未提交源码；最终用户已通知源码冻结、构建中。行号可能随主代理编辑移动；
   应按符号/摘录复核，不把工作树hash或最新源码追认成旧测试EXE身份。
6. 已完成JSON结构/唯一ID检查：250+33+58不变，原body hash/验收ID/offline状态不变；191个
   唯一原生引用均找到符号/摘录，最终移动的1个定位已刷新，静态源hash写入JSON，不是EXE证据。
   文档diff无空白错误。这不是原生测试，也不读取正在运行的临时结果来推断通过。

## 给主代理的确定缺口

### 没有业务消费者的9字段

全部已能导入/导出，以下是**业务绑定缺口**，不是“配置未实现”。具体行号及旧消费者在JSON。

| 字段 | 缺口与实际边界 |
| --- | --- |
| EMU_PATH | 新MuMu绑定接受独立显式路径，未找到此profile字段到绑定的取值链；旧BlueStacks路径替换也不等价 |
| EMU_INDEX | 导入实例索引没有自动成为新版LifecycleTarget/设备绑定；不允许为补证发现设备 |
| ADB_ADRESS | 保留旧拼写/地址值，未找到profile到新版ADB连接参数的绑定 |
| AUTO_START_CLASH | recovery_binding接受独立vpn_required；EnsureVpn枚举/离线端口不等于此开关已驱动恢复，包发现/系统授权实际适配未接 |
| LAST_VERSION | 新版更新/版本回写未实现，M6边界 |
| LATEST_VERSION | 新版更新提示/版本回写未实现，M6边界 |
| FARM_TARGET_TEXT | 原统计显示名没有被新版业务summary读取；目录标题的存在不是这个字段的消费者 |
| WEBSITE_ORG_TIME | 原每周网页提醒/点击后时间回写未接M5 |
| AM_REFRESH_TIME | 原双周刷新提醒/点击后时间回写未接M5 |

### 已有消费者但不等价完整覆盖

| 字段/旧函数 | 当前实现与未承接范围 |
| --- | --- |
| MAX_TRY_LIMIT | dungeon_route/dark_light消费未知页耗尽；map/entry等有限图还有自己的hit/time预算，不能称旧所有FindCoords/状态重试点都受此字段控制 |
| MAX_CRASH_LIMIT | WvdRunState消费累计上限；recovery_binding另传max_crashes，调用者需要证明与冻结profile一致，状态取值本身不证明所有恢复入口一致 |
| LANGUAGE | CombatStrategy/setting_is按语言比较枚举/组名；旧gettext、任务显示名与Tk本地化未完整迁移 |
| FARM_TARGET | 导入时选区段，新TaskHandoff检验来源/改7000G；m4_check目录遍历/编译不等于从此字段启动生产Farm |
| ACTIVE_BEG_MONEY | 新普通路线已有unknown_leap调用、发布binding、状态取值、TaskHandoff/LeapWait源码，未构建/运行；当前dark_light独立未知分发未见同调用，不能称全IdentifyState分支覆盖 |
| SaveImage / SaveDebugImage / SaveDebugImageThrottled | 结构化识别/Run结果存在；故障、稀有矿、鱼获PNG持久化与旧按reason节流尚无等价业务消费者 |
| BagClear_Item | 旧六格物品存仓函数没有新版等价链；固定源未找到明确词法调用也不构成删除授权，不能用组队或LAST_BAGCLEAR冒充存仓 |
| GUI/main/日志/更新器 | RunCoordinator真实停止/原子结果不等于旧Tk start/finished队列、配置编辑、日志展示、版本下载/重启链已迁移；旧生产入口保持 |

重点实现引用：`native/storage/legacy_import.cpp::parse`，
`native/games/wvd/state.cpp::restart_game/observe_unknown_leap`，
`tasks/dungeon_route.cpp::traverse_dungeon`，`tasks/dark_light.cpp::dark_light`，
`recovery/boot.cpp::recovery_binding`，`tasks/task_handoff.cpp::poll`。
具体文件行和调用摘录以JSON为准，以上不是“搜索到名字即证明”的替代证据。

### 已有新代码，不能记成未实现或通过

- Repel/Fordraig/COS/Steel均有源码。最新静态复核已见Fordraig/COS的CLI编译分支、CMake注册、
  WvdRunState确认事件和摘要；多阶段发布及fixture接线已加入，源码冻结构建中。没有新构建/运行结果，不标PASS。
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

最新用户提供的分项统一记在[m4-business-validation.md](../m4-business-validation.md)。
7000G 23/24输入两分支和停止/拒绝的PASS不改变0/58；Golden修正2仍13输入SCENE失败，
达到有限修正边界BLOCKED。牛洞28输入帧龄门禁失败，REST=true未执行。
沙人最初夹具自检失败与钢发布缺bondmate_close是独立失败类型；后续root vg19i0vi的
沙人/钢Failed/UserStopped标签及全部5项身份/静止核对照用户原样保留，不能写全通过。
root gqgh3uz1的17方法/24场景对话/因果/跳跃通过不能解除Golden完整任务阻断。

## 维护约束

本轮不修改历史M1或m4-task-status.json。m4_inventory.py的早期生成逻辑和
m4-semantic-differences.json的历史remaining中仍可能有过期“未接线”用语；本次无权编辑它们，
不得用这些旧remaining覆盖本轮current_refs。历史差异/批准依据仍保留，当前实现状态以本表为准。
以后重生成叠加表须保留并复核semantic_audit，不能机械刷新旧NOT_STARTED或用运行计数填假通过。
不改正式配置/资源/数据库，不增双读双写，不切生产，不进入M5。
