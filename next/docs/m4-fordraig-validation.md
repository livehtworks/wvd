# Fordraig 离线实现与验证交接

当前状态：PARTIAL / INTEGRATION_REQUIRED。构建 NOT_RUN；测试 NOT_RUN；真实质量 UNVERIFIED；release_allowed=false。

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
| 0 | Leap=0 | cursedWheel -> Fordraig/Leap -> 必要时 leap -> OK -> 等待 15 秒 -> Inn |
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
- 固定资源解析核对 `Fordraig/Leap` 与 `fordraig/Leap`、`ReturnText` 与 returnText 的大小写/别名，不复制资产或增加自动回退。两项对话资源也须进入闭包。
- 当前状态、扩展台账、数据权威与共享字段文档由主代理同步。以上接线尚无本轮构建或执行证据，不能据代码存在改写离线验收。

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

所有新增测试均为 **NOT_RUN**，没有新的构建、EXE 身份或通过数。完整 fixture 是待验证输入预期，不能作为功能通过证据。

仍需主代理构建并串行核对：共同 revision 发布/恢复、每个真实后置帧、十段输入与真静止、pending 恢复位置、场景遮挡、Stop/拒绝/旧帧、已领取 ROI 边界、不同配置与 mod、下载/住宿矩阵。boss 路线完成不等于获胜证据，完整夹具必须实际走过战斗及其完成回执。

阶段确认后但根检查点前失败，不能假称正常 Completed 或重放副作用；精确恢复结论待验。真实样本以及 M3 成本、清理、NEXT/Pause、SDK 原生阻塞等既有缺口仍未决。最终保持 release_allowed=false；写集 release 只表示本代理释放编辑所有权，不表示产品发布放行。
