# M4 配置与任务数据映射

固定旧源：`6585f4075f5714ab522aa582993860c09af912c1`。本轮仅静态读取，不 import 旧模块，
不运行测试/构建/设备/网络，不修改旧 config/mod。新版原生库已有有限业务图，不是仅数据导入；
旧 Python 仍是唯一生产入口，完整任务验收仍为 **0/58**。

## 权威与范围

- 当前逐函数/字段去向唯一记录在 [实现叠加表](m4-implementation-map.json) 的 `semantic_audit`。
  下表是其33字段的可读摘要；历史M1索引不改，完整任务状态只读 `m4-task-status.json`。
- `legacy-config-fields.json` 是固定 Git 的字段描述，不是运行配置；33项均由
  `LegacyConfigImporter::parse/export_legacy` 读入/往返，新版 `ProfileStore` 保存显式副本。
- 原 `config.json` 是生产用户配置权威；Run只消费显式冻结values。善恶经已确认回执/CAS写
  绑定的新profile，不写旧配置。来源/passthrough与当前值不构成并行写入权威。
- **9项仅导入/导出，24项找到有范围的业务/选择读取。** 24不表示完整承接，其中包括新转交
  未构建消费者、局部预算和语言比较。字段出现于描述、校验、导出或测试不能计作业务消费。

## 33字段逐项摘要

旧引用均指固定Git文件，不指当前生产文件的同名行；完整读取/写入/回调引用和差异分类在JSON。
当前引用为本次工作树静态快照，按符号及JSON内excerpt复核；并行源码变化可能移动行号。

| 字段 | 旧消费者示例 | 当前业务消费者 / 差异与剩余 |
| --- | --- | --- |
| `EMU_PATH` | `src/script.py:716`，CheckAndRecoverDevice.StartEmulator | 无业务消费者；仅导入/导出。旧模拟器路径/实例/ADB地址（保留旧拼写和值）。 已导入/导出；新MuMu绑定使用独立显式参数，未找到本字段到真实后端的绑定；Metadata/CLEANUP阻断。 |
| `EMU_INDEX` | `src/script.py:472`，MumuIpcScreenshotBackend.connect | 无业务消费者；仅导入/导出。旧模拟器路径/实例/ADB地址（保留旧拼写和值）。 已导入/导出；新MuMu绑定使用独立显式参数，未找到本字段到真实后端的绑定；Metadata/CLEANUP阻断。 |
| `ADB_ADRESS` | `src/script.py:845`，CheckAndRecoverDevice | 无业务消费者；仅导入/导出。旧模拟器路径/实例/ADB地址（保留旧拼写和值）。 已导入/导出；新MuMu绑定使用独立显式参数，未找到本字段到真实后端的绑定；Metadata/CLEANUP阻断。 |
| `AUTO_START_CLASH` | `src/script.py:308`，EnsureClashVpn | 无业务消费者；仅导入/导出。旧EnsureClashVpn开关，已连接则不重复启动。 有EnsureVpn类型及独立vpn_required，无本字段到LifecycleTarget的消费者；真实端口未接。 |
| `LAST_VERSION` | `src/gui.py:508`，ConfigPanelApp.__init__ | 无业务消费者；仅导入/导出。旧更新检查/展示版本字段。 原树保留；新更新/版本回写消费者缺失。 |
| `LATEST_VERSION` | `src/gui.py:1591`，ConfigPanelApp.create_widgets | 无业务消费者；仅导入/导出。旧更新检查/展示版本字段。 原树保留；新更新/版本回写消费者缺失。 |
| `FARM_TARGET_TEXT` | `src/script.py:1843`，Factory.DungeonCompletionCounter | 无业务消费者；仅导入/导出。旧任务显示名/统计文本。 目录标题不等于此字段被消费；新业务summary未读取该显示名，M5 UI未做。 |
| `FARM_TARGET` | `src/script.py:3471`，Factory.QuestFarm | `next/native/storage/legacy_import.cpp:66`、`next/native/games/wvd/tasks/task_handoff.cpp:39`、`next/native/games/wvd/tasks/task_handoff.cpp:198`。旧选择任务/任务专用区段及转7000G目标。 已选择导入区段；新转交检验/更换目标源码未构建。m4_check遍历显式目录不等于从本字段启动Farm。 |
| `KARMA_ADJUST` | `src/script.py:2263`，Factory.IdentifyState | `next/native/games/wvd/state.cpp:13`、`next/native/games/wvd/recovery/karma_prompt.cpp:4`、`next/native/storage/karma_writer.cpp:20`。有符号字符串善恶余额：零/负走ambush，正走ignore，确认后写新值。 确认后只写显式新版profile；CAS失败保留事实不重发，完整任务矩阵未验。 |
| `TASK_SPECIFIC_CONFIG` | `src/script.py:1764`，Factory.ReloadStrategy | `next/native/storage/legacy_import.cpp:70`、`next/native/games/wvd/combat/strategy.cpp:10`。先GENERAL后选任务/DEFAULT；策略选择同时受此开关控制。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `STRATEGY` | `src/script.py:1776`，Factory.ReloadStrategy | `next/native/games/wvd/combat/strategy.cpp:32`、`next/native/games/wvd/combat/turn.cpp:93`、`next/native/games/wvd/combat/strategy.cpp:69`。分组顺序/首同名深复制、技能行和complete_one_as_all。 role/skill/target/level参与真实动作；freq_var固定旧执行不读，保留但不发明频率；全组合未验。 |
| `DEFAULT_OVERALL_STRATEGY` | `src/script.py:1765`，Factory.ReloadStrategy | `next/native/games/wvd/combat/strategy.cpp:23`。非任务专用时选默认策略组。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `RELOAD_STRATEGY_WHEN` | `src/script.py:3132`，Factory.StateDungeon | `next/native/games/wvd/state.cpp:123`、`next/native/games/wvd/state.cpp:167`。每次副本/每场战斗等重载时机，重启/死亡仍重载。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `LANGUAGE` | `src/utils.py:263`， | `next/native/games/wvd/combat/strategy.cpp:8`、`next/native/games/wvd/state.cpp:53`。旧gettext/配置显示语言；运行字符串策略名称按语言对应。 策略/枚举比较已有消费；不代表旧gettext文本、任务名和UI本地化已迁移。 |
| `WEBSITE_ORG_TIME` | `src/gui.py:1366`，ConfigPanelApp.create_widgets | 无业务消费者；仅导入/导出。旧网页访问后的周/双周提醒时间。 原值可往返，新网页提醒/时间回写消费者缺失。 |
| `AM_REFRESH_TIME` | `src/gui.py:1407`，ConfigPanelApp.create_widgets | 无业务消费者；仅导入/导出。旧网页访问后的周/双周提醒时间。 原值可往返，新网页提醒/时间回写消费者缺失。 |
| `TASK_POINT_STRATEGY` | `src/script.py:1767`，Factory.ReloadStrategy | `next/native/games/wvd/combat/strategy.cpp:12`、`next/native/games/wvd/state.cpp:126`、`next/native/games/wvd/tasks/dungeon_route.cpp:58`。overall_strategy与task_point字符串步骤键；确认到点后切换。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `QUICK_DISARM_CHEST` | `src/script.py:3034`，Factory.StateChest | `next/native/games/wvd/tasks/dungeon_route.cpp:75`、`next/native/games/wvd/tasks/dark_light.cpp:16`、`next/native/games/wvd/chest/chest.cpp:6`。旧快速解除宝箱模式。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `WHO_WILL_OPEN_IT` | `src/script.py:3037`，Factory.StateChest | `next/native/games/wvd/tasks/dungeon_route.cpp:74`、`next/native/games/wvd/chest/chest.cpp:6`、`next/native/games/wvd/chest/selection.cpp:19`。旧0随机或指定开箱角色，恐惧时更新候选。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `SKIP_COMBAT_RECOVER` | `src/script.py:3231`，Factory.StateDungeon | `next/native/games/wvd/state.cpp:175`、`next/native/games/wvd/supply/dungeon_recover.cpp:4`、`next/native/games/wvd/tasks/dungeon_route.cpp:58`。true表示跳过战后恢复，不反转保存语义。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `SKIP_CHEST_RECOVER` | `src/script.py:3222`，Factory.StateDungeon | `next/native/games/wvd/state.cpp:181`、`next/native/games/wvd/supply/dungeon_recover.cpp:4`、`next/native/games/wvd/tasks/dungeon_route.cpp:58`。true表示跳过开箱后恢复，不反转保存语义。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `RECOVER_WHEN_BEGINNING` | `src/script.py:3236`，Factory.StateDungeon | `next/native/games/wvd/state.cpp:189`、`next/native/games/wvd/supply/dungeon_recover.cpp:4`、`next/native/games/wvd/tasks/dungeon_route.cpp:58`。首次入本恢复，复活/遭遇恢复事实另计。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `ACTIVE_REST` | `src/script.py:2191`，Factory.IdentifyState | `next/native/games/wvd/supply/policy.cpp:11`、`next/native/games/wvd/tasks/bull_cave.cpp:35`、`next/native/games/wvd/quests/repel_forces.hpp:14`。普通本休息开关；牛洞段数、击退连续双战组数有独立语义。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `ACTIVE_ROYALSUITE_REST` | `src/script.py:2465`，Factory.StateInn | `next/native/games/wvd/tasks/departure.cpp:46`、`next/native/games/wvd/supply/inn.cpp:4`、`next/native/games/wvd/tasks/sleep_visits.cpp:27`。普通/套房住宿选择，不代替应否住宿判断。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `ACTIVE_TRIUMPH` | `src/script.py:4084`，Factory.QuestFarm | `next/native/games/wvd/tasks/bounty_cycle.cpp:71`、`next/native/games/wvd/tasks/bounty_cycle.cpp:95`。蝎女跳跃选择凯旋；BeautifulOre优先，吉尔不使用。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `ACTIVE_BEAUTIFUL_ORE` | `src/script.py:4083`，Factory.QuestFarm | `next/native/games/wvd/tasks/bounty_cycle.cpp:70`、`next/native/games/wvd/tasks/bounty_cycle.cpp:95`、`next/native/games/wvd/state.cpp:662`。蝎女美矿石章/跳点及跨城跳过分支，优先凯旋。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `ACTIVE_BEG_MONEY` | `src/script.py:2239`，Factory.IdentifyState | `next/native/games/wvd/tasks/dungeon_route.cpp:98`、`next/native/games/wvd/tasks/pipeline_compiler.cpp:568`、`next/native/games/wvd/tasks/workflow_session.cpp:81`。未知cursedWheel页转7000G，否则等待7300秒后恢复。 普通traverse_dungeon的未知页路由、binding、状态取值、转交/分段等待已静态接线，尚未构建/运行；dark_light独立未知分发未见unknown_leap调用，全任务覆盖仍有缺口。 |
| `MAX_TRY_LIMIT` | `src/script.py:1503`，Factory.FindCoordsOrElseExecuteFallbackAndWait | `next/native/games/wvd/tasks/dungeon_route.cpp:102`、`next/native/games/wvd/tasks/dark_light.cpp:39`。旧多处寻找/状态检查的重试上限。 当前消费未知页耗尽；其它有限图使用各自hit/time预算，不能据这两处读值称旧所有重试点参数等价。 |
| `MAX_CRASH_LIMIT` | `src/script.py:1715`，Factory.restartGame | `next/native/games/wvd/state.cpp:217`、`next/native/games/wvd/recovery/boot.cpp:106`。旧应用重启累计上限/设备升级阈值。 状态读取已接；recovery_binding的max_crashes仍由调用者另传，未证明每个入口与本配置一致。 |
| `REST_INTERVEL` | `src/script.py:3617`，Factory.QuestFarm | `next/native/games/wvd/supply/policy.cpp:8`、`next/native/games/wvd/state.cpp:58`、`next/native/games/wvd/tasks/steel_trial.cpp:59`。普通本max(interval,1)，巨人/悬赏/钢试炼interval+1，击退按组数，不能混用。 负值/溢出严格拒绝与旧运行异常差异须保留；各专项全矩阵未验。 |
| `ACTIVE_CSC` | `src/script.py:2016`，Factory.CursedWheelTimeLeap | `next/native/games/wvd/tasks/bull_cave.cpp:52`、`next/native/games/wvd/navigation/causality.cpp:5`。只有调用者提供CSC_symbol/setting时启用因果；false仅关闭调整，不取消跳跃。 牛洞调用者消费；未传CSC参数的蝎女/吉尔等走无因果是旧语义，不因出现关键词算全覆盖。 |
| `BYPASS_THE_WALL` | `src/script.py:4525`，Factory.QuestFarm | `next/native/games/wvd/tasks/dungeon_route.cpp:140`、`next/native/games/wvd/tasks/sandman.cpp:36`、`next/native/games/wvd/navigation/wall_bypass.cpp:5`。重启后防空气墙，普通dungeon适用；沙人关闭局部值。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |
| `RE_ASSEMBLE_PARTY` | `src/script.py:4524`，Factory.QuestFarm | `next/native/games/wvd/supply/policy.cpp:20`、`next/native/games/wvd/tasks/departure.cpp:33`、`next/native/games/wvd/tasks/sandman.cpp:35`。累计六小时组队周期，沙人本地关闭；不等价通用存仓。 消费者已有；完整任务及恢复/配置/mod组合仍未验，0/58不变。 |

## 导入与保存

GENERAL后合并选中区段；仅在启用任务专用且目标存在时选择任务区段，否则DEFAULT。
缺字段才应用核实默认值，不用默认值补齐缺字段的CAS保存草稿。未知字段保留完整原树并按
JSON Pointer分类；未知值不会自动变成运行参数。兼容导出修改原来源区段，保留其它区段。

`import_copy`只接受不存在的新目录并核对源hash。ProfileStore以内容revision、独占sidecar锁
和原子安装维护唯一写者；冲突不自动重载覆盖，安装失败不报成功。`STRATEGY`保持首同名组、
顺序、深复制及`complete_one_as_all`，`freq_var`在固定旧执行中不读取，新版不发明频率语义。
`TASK_POINT_STRATEGY`保持`overall_strategy/task_point`与字符串步骤键；SKIP字段不反转保存值。

## 任务与mod

`legacy-quests.json`是固定Git原始字节副本，58项为43 dungeon与15 quest。WvdTaskPlan保留
源树，解析EOT顺序fallback、目标提示及RTT；position/stair点、harken楼梯引用不是普通ROI。
`PipelineCompiler/publish_workflow`已消费这些数据，43项普通入本/路线/迭代图和15基础专项源码
已存在。图存在/可编译不代表完整任务通过，0/58不变。

`fordraig/repelEnemyForces/CaveOfSeperation/steeltrail`是4个源码扩展，不加入固定58项。
新扩展、状态、转交和`publish_workflow_stages`已接线，源码冻结并构建中；多阶段共用一个sealed
bundle revision，各正常段独立entry/checkpoint/time/dialogue binding，保留runtime同revision限制。
未构建/未验证与没有实现是不同状态，见[审计说明](m4-semantic-audit.md)。

任务mod按显式文件顺序追加，冲突名反复加`_mod`/`_自定义`，中英名称按旧规则补齐，错误项留下
诊断，不覆盖基线。图片按基础原名、基础alias、mod原名/alias查找，由持锁发布保存实际来源。
运行只读已封存副本，不扫描用户mod；目录模板在冻结Run前展开为派生revision，活动期不接目录。

## 状态与副作用

WvdRunState由RunCoordinator独占，正常有限段保留业务事实，新代次不保留帧、坐标、缓存或未确认
技能选择。WvdConfirm在当前帧身份/时效复核后写状态；RunStore在真静止后保存结果，不能用活动
事件窗口代替完整输入计数或结果权威。

普通本完成次数、专项开始次数、已确认周期/奖励、付款/跳跃/转交待确认意图分别记录，不能混算。
住宿、组队、角色恢复、默认/专项对话、善恶CAS、未知页冻结出口已存在；尚未完成的是全部任务
恢复/配置/mod/连续周期对账，不应继续笼统写成这些链都未接线。旧PNG保存、Tk日志/配置编辑、
更新器仍无完整新版消费者。状态验证范围另见[生命周期报告](../m4-state-validation.md)，当前静态
取值点以本轮叠加表为准，历史报告未覆盖的新源码不据此认领通过。

`ACTIVE_BEG_MONEY`已有新意图/转交/7300秒分段等待消费者源码，不再记为仅数据；冻结来源、
发布、WvdUnknownLeap绑定和boot映射已有接线；旧Run结果提交后启动下一Run及全任务覆盖仍待验证。不得重读另一配置
区段、双写旧config或从历史结果自动重放。Metadata/CLEANUP及RESOURCE/PERFORMANCE未决继续保留。
