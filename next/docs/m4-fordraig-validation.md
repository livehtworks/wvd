# Fordraig 离线实现与验证交接

当前状态：PARTIAL / FULL_WORKFLOW_BLOCKED。主代理已运行资源修正后的首次完整业务 fixture：Stage0 完成，Stage1 住宿完成后，打开公会的输入因 `STALE_ACTION_INTENT` 被门禁拒绝，根终态 `Failed / CUSTOM_ACTION_FAILED`。本次只读归因及文档更新，没有源码修正或复验；真实质量 UNVERIFIED；release_allowed=false。

本专项是显式 `fordraig` quest/mod 扩展，不是基础地下城 `fordraig-B3F`。固定 58 个 ID、旧 Python 生产入口、配置、资源和用户文件不在本代理写范围内。本代理未构建、未运行测试、未调用 EXE、未操作设备、未提交或推送；主代理及其他代理的接线不作为本代理的执行证据。

## 来源与写范围

已实际读取工作包固定 `legacy/src/script.py`：`case "fordraig"` 位于 3545-3613，`StateAcceptRequest` 位于 3397，`TeleportFromCityToWorldLocation` 位于 1935，后备动作执行器位于 1483。上述内容是源码依据，不是运行结果。

首轮实现只写：
- `native/games/wvd/quests/fordraig.hpp`
- `native/games/wvd/tasks/fordraig.hpp`、`.cpp`
- `native/games/wvd/tasks/featured_request.hpp`、`.cpp`
- 本文档。

后续中文化与测试轮只写：
- 本文档。
- `native/games/wvd/tasks/fordraig.cpp` 中新增的两条注释，业务标识符与逻辑不变。
- 新建 `tests/m4/test_fordraig.py`。
- 新建 `tests/native/test_m4_fordraig.cpp`。

共享 state、识别器、对话策略、编译器、CMake、CLI 和既有测试文件均由主代理接线，本代理不修改。

## 公开接口

`wvd::games::tasks` 中新增：

```cpp
WvdTaskPlan fordraig_plan(const WvdQuestDefinition &definition);
std::vector<CompiledWorkflow> fordraig_cycle(const WvdQuestDefinition &definition,
    const nlohmann::json &profile, const std::set<std::string> &images,
    bool allow_download = true);
void configure_fordraig_units(runtime::RunDefinition &definition,
    const std::vector<runtime::SessionDefinition> &stages, std::size_t cycles = 1);
```

计划要求 `id == "fordraig"`、`type == "quest"`，保留世界地图、入本和按旧源顺序排列的全部 11 个任务点。`fordraig_cycle` 按下表返回十份独立编译的阶段图，其中六份调用真实 `traverse_dungeon`。不会把拼接路线作为同一个 boss 策略作用域，也不会把六份完整战斗、宝箱与恢复子图复制进同一份受 4096 节点限制的图。

`FeaturedRequest::Fordraig` 复用既有 `accept_featured_request(FeaturedRequest, bool royal_suite)`。包含真实强制住宿、三次 `(150,1000)->(150,200)` 滚动、细滚动、目标相对 accepted 检查、`(350,180)` 选择偏移和返回城内。已领取时不再选择，但仍完成本次访问。`accepted.image` 与 `eligible.image` 为 `fordraig/RequestAccept`，不再错用 `LBC/request`；BullCave、GoldenChest 保留原行为。

`wvd::games::quests::FordraigCycle` 提供：

```cpp
static constexpr std::size_t units_per_cycle = 10;
void start(std::size_t unit, std::size_t visits);
void prepare(Phase phase, std::size_t unit);
void advance(Phase phase, std::size_t unit, std::size_t visits, std::size_t points);
static std::size_t route_points(Phase phase);
bool force_automatic() const;
std::size_t sequence(bool start = false) const;
bool continuation_ready(std::size_t completed_unit) const;
nlohmann::json summary(std::size_t unit) const;
```

状态修改只由既有运行状态所有者在新帧确认后调用，不建立新执行器。`advance` 接收正在完成的阶段，不是目标阶段。默认状态为 inactive/Completed；错误阶段、错误 unit、重复准备、缺少 pending、路线数量不符、缺少访问回执及跨周期跳段均拒绝。

## 正常段与共同封存

每轮严格使用同一 RunCoordinator 的 **10 个正常段**。主代理已明确 runtime 要求同一 Run 的 `bundle.revision` 一致，因此必须使用其接入的 `publish_workflow_stages`，一次发布命名空间隔离的多份 pipeline，取得共同封存 revision；各段保留自己的入口、检查点、时间预算和对话 binding。

**不得将十份图分别发布为十个独立 bundle。** 此要求替代首轮独立目录发布建议。阶段定义顺序不得改变，也不能让各段分别创建新 RunCoordinator。

将共同发布所得的十份 SessionDefinition 传给 `configure_fordraig_units`：第 0 段设为 initial，后续按 `unit % 10` 选择。接受 1-25 轮，即 10-250 段；拒绝 0、溢出、重复配置、错误阶段数量、空入口或检查点和超预算定义。正常推进不使用 RecoveryRequired。

| 段偏移 | 类型化阶段 | 业务与确认 |
| --- | --- | --- |
| 0 | Leap=0 | cursedWheel -> fordraig/Leap -> 必要时 leap -> OK -> 等待 15 秒 -> Inn |
| 1 | Request=1 | 强制住宿、领取指定请求、确认访问完成 |
| 2 | Enter=2 | labyrinthOfFordraig -> Entrance -> GotoDung -> 地下城 |
| 3 | Trap1Route=3、Trap1Push=4 | 左上 position (721,448)、position (720,608)，返回键关图后触发机关 |
| 4 | Trap2Route=5、Trap2Push=6 | 左上 stair_down (721,236)、左下 position (240,921)，返回键关图后触发机关 |
| 5 | Trap3=7 | 左下 position **(33,1238)**、stair_down (453,1027)、position (187,1027)、stair_teleport (80,1026) |
| 6 | PreBoss=8 | 左下 position (508,1025) |
| 7 | Boss=9 | 左下 position (720,1025)，使用原策略 |
| 8 | Exit=10 | 左上 stair_teleport (665,395) |
| 9 | Return=11 -> Completed=12 | 返回键、ReturnText、City_RoyalCityLuknalia、Inn |

前两机关保留顺序后备链：`(100,250)->(800,250)` 滑动，再点击三次 `(400,800)`，随后重新识别 `fordraig/TryPushingIt`。每个输入仍需新帧场景许可；点击机关后要求提示消失且地下城可见。不是宝箱拆陷阱动作，第三陷阱不添加虚构点击。

回城保留 leaveDung、`(455,1200)` 和 3.75 秒节奏。旧助手的后备字符串 `return` 表示 `PressReturn()`，用 `graph.back` 承接，不猜模板坐标。世界地图缩放未新增，遵守工作包禁止调整缩放及既有导航职责。

单 Session 至多包含一份约 1300 秒的地图子图。Trap1/2 在子图预算上加 300 秒，包含 180 秒机关子图；其他段加 120 秒。当前十段预算依次为 **300、360、180、1600、1600、1420、1420、1420、1420、300 秒**，超过 1800 秒直接拒绝，不截断或放宽。Leap 的 15 秒等待拆成 10 秒和 5 秒两个节点，遵守单节点延迟上限；帧 TTL 和停止预算不变。

## 状态接线契约

在 `WvdRunState` 持有一个 `quests::FordraigCycle fordraig_`。以下 **16 个事件**同时进入 `PipelineCompiler::confirm` 白名单与 `WvdRunState::confirm_event` 消费者：

| 事件 | 所有者调用 |
| --- | --- |
| fordraig_started | `start(unit_index_, visits_completed)` |
| fordraig_leap_prepared | `prepare(Phase::Leap, unit_index_)` |
| fordraig_leaped | `advance(Phase::Leap, unit_index_, visits_completed, task_step_)` |
| fordraig_requested | `advance(Phase::Request, ...)` |
| fordraig_entered | `advance(Phase::Enter, ...)` |
| fordraig_trap1_routed | `advance(Phase::Trap1Route, ...)` |
| fordraig_trap1_prepared | `prepare(Phase::Trap1Push, unit_index_)` |
| fordraig_trap1_completed | `advance(Phase::Trap1Push, ...)` |
| fordraig_trap2_routed | `advance(Phase::Trap2Route, ...)` |
| fordraig_trap2_prepared | `prepare(Phase::Trap2Push, unit_index_)` |
| fordraig_trap2_completed | `advance(Phase::Trap2Push, ...)` |
| fordraig_trap3_completed | `advance(Phase::Trap3, ...)` |
| fordraig_preboss_completed | `advance(Phase::PreBoss, ...)` |
| fordraig_boss_completed | `advance(Phase::Boss, ...)` |
| fordraig_exited | `advance(Phase::Exit, ...)` |
| fordraig_completed | `advance(Phase::Return, ...)` |

省略的 advance 参数均与第一条完整调用相同。`visits_completed` 来自 `featured_visit_.summary()`，不是 selections_confirmed，因为已领取访问同样有效。六段路线数必须严格为 2、2、4、1、1、1，图也会先检查 `/task_step` 再提交视觉确认。既有 dungeon、target、combat、featured、inn 与 special-dialogue 事件仍由共享消费者处理。

- started 检查没有未结算战斗、宝箱、付款、featured 或对话副作用；按既有单调时间开始本轮并只增加一次尝试/副本计数。阶段确认不是新增副本完成。
- requested 要求 featured 访问已结束且完成数恰好增加一次。路线和回城确认拒绝未结算副作用，不能仅凭地图/Inn 位图放行。仅 completed 在返回 Inn 后增加 completed_cycles，不新增最终住宿。
- 确认 ID 加入 `:fordraig:<sequence(event == "fordraig_started")>`，保留 operation、unit 和路线生命周期身份。不得用 generation/frame ID 重新标识副作用，或清掉 pending 后重放。
- 正常续段核对 `continuation_ready(previous_unit)`，再递增 unit；根检查点和真静止仍由 RunCoordinator 证明。类型化阶段跨 Session 保留，局部 task_step 仍由地图子图的 dungeon_entered 重置。
- 普通恢复与生命周期恢复保留当前阶段和 pending。根 Pending 报告 `quest.fordraig_side_effect_unconfirmed`，不重放未确认的 Leap/Push。Trap1/2 路线完成后进入同 unit 的 Push，恢复不能回到路线起点。

发布 `summary["fordraig"] = fordraig_.summary(unit_index_)`，共 **11 个字段**：`active`、`phase`、`pending`、`leap_pending`、`trap_pending`、`unit_matches`、`expected_unit`、`force_automatic`、`completed_cycles`、`sequence`、`continuation_ready`。

业务条件新增白名单为 `/fordraig/active`、`/fordraig/phase`、`/fordraig/pending`、`/fordraig/unit_matches`；其余字段只作诊断，除非有显式消费者。

SYSTEMAUTOCOMBAT 由摘要派生，不修改冻结 profile 或策略对象：

```cpp
auto strategy_summary = strategy_.summary();
strategy_summary["automatic"] =
    fordraig_.force_automatic() || strategy_summary.at("automatic").get<bool>();
// 将此局部值发布为 summary["strategy"]。
```

take_turn 与 fight_encounter 已消费 `/strategy/automatic`。非 boss 强制 Auto；boss 返回原策略的实际 automatic 值，因此原本自动的 boss 仍自动。不能强制 boss=false、替换策略行、建立第二份 profile 或在观察时补满已消费频率。boss 完成后恢复强制 Auto，inactive 不影响其他任务。

## 主代理其他接入项

- DialoguePolicy::Fordraig、名称 fordraig、双向解析和有序选项 `fordraig/thedagger`、`fordraig/InsertTheDagger`。根图、地图、机关、返城和领取均使用该策略；识别、资源闭包、冷启动与恢复包装必须保留，不能退回 Default。
- featured_request_accepted 的参数验证和实际目标识别均允许且使用 `fordraig/RequestAccept`，保留原 LBC/request；不能仅扩白名单但继续识别 LBC。维持目标纵向 +/-200 的 request_accepted ROI 语义。
- CMake 加入业务源码和独立 `test_m4_fordraig` 可执行目标；测试链接现有 M4 原生库、SDK/OpenCV 依赖和 UTF-8 工具链设置，不引入第二套框架。本测试有独立 main，不能把它的源码并入已有测试 main。
- 已接的 `workflow=fordraig` 使用显式 quest_catalog，并通过 `publish_workflow_stages` 共用封存 revision、十段同一 Run。恢复选择当前 `unit % 10` 的业务图，不一律回到 Leap。
- 生产 Leap 直接引用正式 `fordraig/Leap`；返城直接引用 `ReturnText`。共享子图的 `returnText.png` 仅使用正式 manifest 已有的 `returnText.png -> ReturnText.png` 别名；不新增大小写兼容或自动回退。两项对话资源也须进入闭包。
- 当前状态、扩展台账、数据权威与共享字段文档由主代理同步。已有统一构建和纯状态结果不代表以上完整流程接线通过。

## 新增测试与执行协议

已审查既有 test_state、test_plan、test_workflow 和对应原生实现；不修改或禁用它们。

独立 `test_m4_fordraig.cpp` 接收一个输入 JSON 路径：
- 必需键：`case`、`source`、`descriptor`、`output`。
- case 为 `domain`、`state`、`pending` 或 `plan`；state 可选 `recovery=true`。
- plan 另需 `quests` 和 `manifest`，只读基础 58 项和资源清单，使用真实 mod 合并、WvdTaskPlan、PipelineCompiler、资源解析与 configure_fordraig_units。
- source 使用既有 LegacyConfigImporter 的 GENERAL 结构，生成完整 profile。
- output 必须是新的绝对路径。任何设备/binding/execute 参数均拒绝。
- 不创建 DeviceBackend、Controller、RunCoordinator，不发布资源包，不调用 Maa 执行器；直接调用真实 WvdRunState 的事件 API。
- 输出 `case`、`result`、`workflow_executed=false`、`outcome`。只有全部断言成功才写 PASS；异常返回非零，不补默认结果、不跳过失败。

Python 测试使用 `next/.local/m4-fordraig-*` 下的独立临时目录，UTF-8 写入输入和结果；核对输入/基础清单不变，记录 EXE 前后 SHA256、退出码和完整日志。原生目标为 `next/build/m4/Release/test_m4_fordraig.exe`，不存在时直接失败，不回退旧 EXE。这些路径和命令仅供后续执行，本代理没有运行它们。

纯状态覆盖 domain 错误阶段/unit/pending/路线计数，以及真实 WvdRunState 双周期、20 段、幂等回执、一次已领取访问、跨 Run 隔离、非 boss Auto、boss 原策略、已消费行不补满、恢复 pending 和旧代次/零帧拒绝。传给状态 API 的帧号仅为契约输入，不冒充视觉证据。

计划覆盖显式 mod 从 58 到 59 条但不改变基础项、全部 11 个坐标与方向、十图顺序、16 个事件、资源闭包、独立预算、accepted image、三次滚动、350/180 偏移和错误配置拒绝。同一计划用例另调用真实 wvd_m4_check 的 compile_specials_manifest 接口，验证扩展行的 stages、required_normal_units=10 和全段 images/required_actions/missing_images 并集；不再把扩展行视为单个 nodes。

完整因果 fixture 使用 `WorkflowTests.execute` 组合调用（当前 helper 名为 execute，不是 run_case），不继承 WorkflowTests，不运行其包含 git 的 setUpClass。图像资源按当前固定 manifest 的 14 项基础对话配置，所有素材写入专用测试目录。完整夹具包含 Leap/住宿/请求/入本/两机关/第三陷阱/对话/preboss/boss/出口/返城；非 boss 使用真实 Auto 输入和输入后四秒时间事件，boss 预期手动防御，错点由因果后端拒绝。另有跳跃拒绝保持 pending 的夹具。

## 未验证项与发布边界

主代理报告统一 `14e9eb3` 的纯状态 5 PASS；本代理只读核对 `next/.local/m4-fordraig-tk6nvnye/plan/native.log`，保留计划原轮的 `FORDRAIG_RESOURCE_MISSING:Fordraig/Leap.png` 失败。该通过数不归为本代理执行，也不覆盖计划或完整流程。

此前资源修正仅修改 `tasks/fordraig.cpp`、`tests/m4/test_fordraig.py` 和本文档：Leap 生产引用及两个因果 fixture 统一为小写目录，删除夹具专有 Leap 别名；返城别名方向与正式 manifest 一致。计划断言同时核对 Leap/ReturnText 的精确解析路径，保留主代理的 dual import。未改资源清单、共享代码或原生测试。

此前资源修正的只读静态检查：`fordraig.cpp` 的 18 个直接 `C::image` 引用均精确命中 manifest；Python AST 解析正常，仍为 8 个测试方法。没有导入或执行测试模块，没有运行原生程序；这些检查不计测试 PASS。

完整 fixture 已静态核对住宿、三次带 400ms 时长的请求滚动、350/180 偏移、11 个路线点、两次机关操作、对话、两种战斗输入、返城及输入后时间事件。静态检查不证明实际 Maa 识别、后置帧或十段续接可用。完整 fixture 的首次实际失败见下节；本代理没有新增执行或通过数，也不推断同批其他测试结果。

## 首次完整业务失败归因

证据根：`next/.local/m4-fordraig-9v35_53l/fordraig-full`。只读核对 `input.json`、`output.json`、`execution.json`、`compiled/pipeline/stage1.json` 和 `run/CF28C9CE-8225-4A6B-8265-A37DB5776C69/1/result.json`。记录的 EXE SHA256 为 `570172a2123c8011ca34745ab69500210769d61e419305cef701f0b6c11318ec`；进程 exit=0 仅表示驱动写出结果，业务结果仍为 Failed。

- backend_calls=9、cursor=9、mismatch=false、generation=2、completed_business_units=1。Stage0 四次 Leap 输入已完成；Stage1 五次住宿输入已完成。两段均使用 revision `5aeaac8aac7112e93a4337854c3f16cc97a1e15b48475101562c2b0981ea8b09`，不是多包 revision 不一致或恢复重放。
- 第九次输入为住宿结束 BACK，frame-9 为 Inn 与 guild；下一条第十次输入预期 `(220,512)`。识别得到 guild box `[200,500,40,24]`、score 约 0.9999995，中心与预期完全一致。没有后端错点，也没有执行第十次输入。
- 旧源 `StateAcceptRequest` 3397 起先 `StateInn()`，再以 guild 作为找到 guildRequest 的后备；fordraig 3560 起调用该函数。此处 fixture 与旧顺序相符；三次滚动、350/180 偏移和 accepted ROI 尚未执行，不能据本轮失败修改这些语义。
- 最后确认是 `inn_rest_completed`，inn_rests=1、featured_visit.active=true、visits_completed=0、selections_confirmed=0；Fordraig 仍为 Request，completed_cycles=0。结果已落盘且 quiescent=true，无 storage_error 或 secondary_errors。

首因位于 `Stage1_RequestWork_Guild` 的 GuardedAction：

| 事件 seq | 事实 | 相对 frame 36 |
| --- | --- | --- |
| 772 | frame.captured，epoch=5 | 0ms |
| 775 | 场景复合识别 Hit；阻塞反证、guild、guildRequest 缺失均成立 | 1604.3ms |
| 780 | 同一帧目标 guild 再识别 Hit | 1939.5ms |
| 783 | intent.requested，frame_id=36 | 1940.5ms |
| 787 | input.rejected：STALE_ACTION_INTENT | 2156.6ms |

场景 Custom 识别本身约 1378.4ms，目标识别约 109.2ms，另有约 669ms 的识别调用及原生动作提交间隔。seq801 的 INPUT_CLOSED 及 native.log 末尾 screencap 错误出现在首因之后，不改写为根因。

当前证据指向共享 `runtime/guarded_action.cpp` 的同帧场景/目标识别与原生动作提交耗时，最终由 `devices/input_gate.cpp` 的帧龄检查正确拒绝。局部图中的 guild 重复匹配确实存在，但其场景内与菜单反证已并行，仅删一项不能证明消除 157ms 超限；不能删除阻塞/菜单反证、猜 ROI、延长 TTL、改成固定点或让 fixture 提前出现 guildRequest 来绕过本次失败。主代理需在共享所有者中核对等价去重/调用开销，本代理不改共享文件、不另造执行通道。

这是首次实际业务失败；本代理本轮根因修正次数为 0，仅定位并报告权限外阻断。后续根因修正由主代理统一计入用户规定的最多两次上限，不以不同文件名或额外复跑重置计数。

## 定点复验后的最小接续

先重建主代理修正并运行原完整 fixture，保留其原始全部断言。当前运行批次的 Python 已加载，不能把之后的源码编辑算入该批证据。本代理不提前扩展配置矩阵。

- 直接复用 full_fixture 的前十次输入：在第十次公会点击分别注入 reject=True 或 stop_after_calls=10，要求后端实际到达该输入、已住宿一次、领取/访问/周期完成仍为零、正确失败或停止且静止。不能让先前 STALE_ACTION_INTENT 导致的九次输入冒充拒绝用例通过。
- 领取副作用反例复用到 `(770,992)` 的前缀并拒绝该输入，要求 featured_visit.pending=true、无后续 BACK/入本或重复领取。现有 Leap 拒绝用例继续保留，不重复添加同义用例。
- 周期入口需求：当前共享 `test_m4_workflow.cpp` 对 Fordraig 调用 `configure_fordraig_units(definition, stage_sessions)`，默认一周期；normal_units=20 无效。主代理可将显式 cycles 传入既有第三参数，并按实际总段数计算测试看护，十个阶段仍共用同一封存 revision，不新建 Run。入口接妥后才拼接第二轮，处理 Inn→轮盘的真实输入、已消费角色和时间事件索引，不添加暂不生效的参数或占位测试。

仍需主代理构建并串行核对：共同 revision 发布/恢复、每个真实后置帧、十段输入与真静止、pending 恢复位置、场景遮挡、Stop/拒绝/旧帧、已领取 ROI 边界、不同配置与 mod、下载/住宿矩阵。boss 路线完成不等于获胜证据，完整夹具必须实际走过战斗及其完成回执。

阶段确认后但根检查点前失败，不能假称正常 Completed 或重放副作用；精确恢复结论待验。真实样本以及 M3 成本、清理、NEXT/Pause、SDK 原生阻塞等既有缺口仍未决。最终保持 release_allowed=false；写集 release 只表示本代理释放编辑所有权，不表示产品发布放行。
