# CaveOfSeperation 源码扩展：实现与集成交接

## 当前结论

- COS 图、阶段对象、识别/对话停点闭环和离线测试源码已实现；主代理统一状态注册、多图发布及构建。`implementation=AWAITING_INTEGRATION_VALIDATION`、`offline=NOT_RUN`、`real=UNVERIFIED`、`release_allowed=false`。
- 旧业务 `6585f4075f5714ab522aa582993860c09af912c1`，已实际读取私有工作包 `legacy/src/script.py:3957-4044` 和 CursedWheelTimeLeap、IdentifyState、StateDungeon。Git blob 与固定提交均为 `0f80961a63d06f26ce7d76e33677dc9cfb9bdf72`。
- 开工 HEAD 为 `695295d81abba05b0e87d88aee06fc9d3d748d28`。并行工作区已有修改均保留；此处不是构建产物身份。
- 本代理未改 state、CMake、CLI、已有测试或基础58项目录。按追加授权修改 vision、dialogue、资源闭包并新增测试源码。没有启动构建/测试、设备调用、生产文件写入、commit/push 或子代理。设备连接和输入均为0。
- `workflow_session.cpp/.hpp` 已释放给主代理，不再编辑；主代理已提供 `publish_workflow_stages` 同封存 revision 接口。本代理不改变 RunCoordinator 的 revision 约束。
- `UnknownLeap` hook 明确留给主代理。本任务没有修改 UnknownFrozen 的实现，也没有新增未知页分支。

## 文件与公开接口

- 新增 `native/games/wvd/quests/cave_of_separation.hpp`：Run 所有的阶段、周期、意图和分段回执，不拥有设备或解释节点。
- 新增 `native/games/wvd/tasks/cave_of_separation.hpp/.cpp`：六个有限 Maa 业务图、原坐标计划、冻结策略选择、RunCoordinator 正常段配置助手。
- 修改 `native/games/wvd/tasks/dungeon_route.hpp/.cpp`：末尾可选 `DungeonTaskStop task_stop = None`；旧省略参数和已有策略调用保持默认图语义。`CaveEna` / `CaveRequest` 仅为明确图片观察；未知枚举报 `DUNGEON_TASK_STOP_INVALID`，策略不匹配报 `DUNGEON_TASK_STOP_POLICY_MISMATCH`。
- 修改 `native/games/wvd/recovery/dialogue_policy.hpp`：Fordraig、COS 分阶段名称、`dialogue_task_stops()`。原枚举顺序和原策略名称保留；没有新增全局运行状态。
- 修改 `native/games/wvd/vision/recognizers.cpp`：观察专用 `task_stop`、复合出口停点优先和输入条件排除；保留 EvaluationMemo、in_parallel 及专用选项批处理。Fordraig 领取识别允许 `fordraig/RequestAccept`，使用该锚点，原 LBC 限制、ROI 和阈值保留。
- 修改 `native/games/wvd/recovery/dialogue.cpp`：默认/特殊对话入口和正常动作后继返回停点；特殊选择后需停点新帧且原选项消失才清除意图，不点击同时出现的 bondmate_close。
- 修改 `native/games/wvd/tasks/pipeline_compiler.cpp`：仅资源索引部分的 finish/validate 增加 dialogue_task_stops；事件注册属于主代理。此前 workflow_session.cpp 增加的同类资源闭包已交主代理继续维护。
- 新增 `tests/native/test_m4_cave_of_separation.cpp` 和 `tests/m4/test_cave_of_separation.py`，均 NOT_RUN；主代理需把前者接入 native 构建目标 `test_m4_cave_of_separation`。未修改已有 test_m4_workflow.cpp。

## 正常段定义

调用 `cave_of_separation_segment(definition, profile, images, segment, allow_download)` 分别编译六段，再一次发布。只允许显式源码扩展 ID `CaveOfSeperation`、type `quest`，不修改或借用基础 TaskID。

| Segment（按顺序） | Phase | 冻结 dialogue_task | 单段预算（未包含可选 boot 包装） | 正常段终点 |
| --- | --- | --- | --- | --- |
| Preparation=0 | Leap/Fortress/RoyalCity/Request/Rest/Enter | 空，即 Default | 1500秒 | 入洞的新帧确认 |
| B1=1 | B1 | CaveOfSeperation.outbound | traverse预算+180秒，当前约1480秒 | 4点及最终 through_stair |
| B2=2 | B2 | CaveOfSeperation.ena | 同上 | COS/EnaTheAdventurer 新帧 |
| B3=3 | B3 | CaveOfSeperation.request | 同上 | COS/requestwasfor 新帧 |
| Back=4 | Back | CaveOfSeperation.return | 同上 | 7点及最终 reached |
| ReturnCity=5 | ReturnGuild/ReturnInn | CaveOfSeperation.return | 360秒 | 找 guild、点击 guild、退出到 Inn |

准备段保留 GhostsOfYore、COS/ArnasPast；ACTIVE_CSC=false 使用原无因果分支，true 使用现有有因果慢路径（空 options，即只关因果）。已有跳跃目标快路径仍按旧源码不调整因果；保留跳跃后10秒等待、返要塞、王城移动 `[450,150,500,150]`、找 guild、领取/退出、强制住宿和两步 COS/COS、COS/COSENT 入洞。

前半三个策略均只点击 `COS/takehimwithyou`；B2/B3 的停点不是可点击选项。后半只点击 `COS/requestwasfor`。Preparation 保持旧代码设置特殊对话之前的 Default。

不能把六段 append 到同一个冻结 Session，不能运行时修改 `dialogue_task`，不能六次独立 publish 获得不同 revision。使用主代理的 `publish_workflow_stages` 一次封存六图（必要时各图先按自身策略包装 boot），再调用 `configure_cave_of_separation_units(run, sessions, cycles)`；默认 cycles=1，最多42周期/252段，超额拒绝、不截短。助手校验六份非空 bundle.revision 相同，每段恰有一个匹配策略的 WvdVision binding、正预算且<=1800秒、非生命周期段、终点和检查点。共享 publisher 负责每阶段 next/on_error/RunChild.entry/reset_hit_counts/opid 命名空间隔离、每图独立4096节点验证。每段 Completed、真静止、业务检查点确认后才可续段；恢复不是正常下一段。

可选 boot 包装当前再加120秒，准备段1620秒、路线约1600秒，仍低于1800秒。不得把四次 traverse 相加塞入一段或放宽父预算。

## 旧路线逐点对照

全部方向是原地图拖动方向；未改变缩放或 ROI。

| 原局部路线 | 新计划（按顺序） |
| --- | --- |
| cosb1f | position 右下 (232,440)；position 右下 (819,707)；position 右上 (605,501)；stair_2 右上 (72,342) |
| cosb2f | position 右上 (394,448)；position 右上 (446,1088)；position 左上 (452,766) |
| cosb3f | stair_3 左上 (720,822)；position 左下 (239,600)；position 左下 (185,1185)；position 左下 (560,652) |
| cosback2f | stair_2 左下 (827,547)；position 右上 (394,448)；position 右上 (446,1088)；position 左上 (452,766)；position 左上 (559,1087)；stair_1 左上 (666,448)；position 右下 (660,919) |

四条路线分段，未拆开单次 StateDungeon，因此任务点策略和每次入本重置仍由原 `dungeon_entered` 事件承接。B2/B3 可以在坐标路线中途或入口已显示停点：不要求全部点完成，也不使用上一条路线残留的 task_step 证明停点。没有目标停点时，即便子图因 Outside/路线耗尽而返回，也不能推进任务。

## 视觉停点闭环与集成边界

识别器、共享对话和路线停点已接入；以下为实现契约及尚需主代理核对的 boot 边界。尚未运行验证，不能宣称停点全链已通过。

1. 冻结 WvdVision 的 `dialogue_task` 经 `dialogue_policy_from_name()` 选择 `dialogue_task_stops()`。已接入 blocking_screen（含 parallel_basic）、map_route_post、auto_route_post、dialogue_post、special_dialogue_post、boot_post；default_dialogue、special_dialogue、movement_stopped、auto_route_moving、reached、through_stair、boot_ready 在活动停点上返回 NoHit。直接 task_stop 的 Hit 明确 action_eligible=false，不提供点击授权；Error/NoHit 区分保留，未知策略仍拒绝，不映射 UserStopped。
2. boot.cpp 由 Goodall/主代理拥有，本代理未编辑。已观察到 clear_common_screens 入口和动作后继以及 restartable 的停点 hook。冻结前仍需修正/确认：TaskStop 只能插入正常 next，不能插入 on_error 以免吞输入失败；各普通固定输入的本地新帧授权也要排除活动停点，不能仅依赖 Entry 扫描。已通过工作消息请主代理转达；当前无直接代理消息工具。独立 boot 终点不是 COS 完成。
3. 纯默认对话识别/点击授权在活动停点帧上必须排除该停点；不能把 `COS/requestwasfor` 放进 B3 的 special_dialogue_options。Back 段则相反：无停点、允许选择 requestwasfor。并发出现普通选项、takehimwithyou 与停点时，停点优先且零输入。
4. `traverse_dungeon` 的 TaskStop 使用图片观察，仅返回正常子终点；外层 `Confirmed` 是独立 WvdConfirm 新捕获帧，且排除战斗/复活，才发 `cos_ena_confirmed` / `cos_request_confirmed`。图中未增加任何停止请求或 UserStopped 映射。
5. 编译器 finish/validate 资源闭包已同时收集对话选项及 `dialogue_task_stops()`；发布时的同类闭包已交主代理维护，包含 standalone common/boot 策略图隐式资源。缺失/损坏模板必须拒绝，不能编成无停点完成。

Fordraig 公开名称为 `DialoguePolicy::Fordraig` / `fordraig`，有序选项准确为 `fordraig/thedagger`、`fordraig/InsertTheDagger`，没有停点。无需向 COS 图中添加 Fordraig 行为。

## 主代理必接：状态与确认事件

在 WvdRunState 持有 `quests::CaveOfSeparation`，摘要键 `/cave_of_separation`。PipelineCompiler::confirm 白名单及 WvdRunState::confirm_event 必须同时登记下列17项事件；通过现有 WvdConfirm 真实新帧链调用，不增加新的 Custom 执行器。

| 事件 | 阶段对象调用/前置 |
| --- | --- |
| cos_started | start(unit_index)，重置本周期住宿回执并递增 supply_cycle；无未确认付款/对话/其它有副作用操作；按既有周期口径增加 dungeons/lap |
| cos_leap_prepared | prepare_leap(unit) |
| cos_leaped | advance(Phase::Leap, unit) |
| cos_fortress | advance(Phase::Fortress, unit) |
| cos_royal | advance(Phase::RoyalCity, unit) |
| cos_request_observed | request_observed(unit)，仅 COS/Okay 或 guildRequest 新帧 |
| cos_request_prepared | prepare_request(unit)，仅 COS/Okay 新帧，在发送 Okay 前保留意图 |
| cos_requested | advance(Phase::Request, unit)，只在退出到 Inn 后清理意图 |
| cos_rested | advance(Phase::Rest, unit, task_step, inn_rest_completed && !inn_payment_pending) |
| cos_entered | advance(Phase::Enter, unit)，不在此复用上轮任务点回执 |
| cos_b1_completed | advance(Phase::B1, unit, task_step)，必须4点 |
| cos_ena_confirmed | advance(Phase::B2, unit, task_step, false, "COS/EnaTheAdventurer") |
| cos_request_confirmed | advance(Phase::B3, unit, task_step, false, "COS/requestwasfor") |
| cos_back_completed | advance(Phase::Back, unit, task_step)，必须7点 |
| cos_guild_prepared | prepare_guild(unit)，在最后一次点击 guild 前保留意图 |
| cos_guild_entered | advance(Phase::ReturnGuild, unit)，新帧 guildRequest 后清理意图 |
| cos_completed | advance(Phase::ReturnInn, unit)，确认 Inn 后唯一 completed_cycles++ |

仅接受上述明确事件，不要建立宽泛 `cos_*` 自动成功分支。未知事件必须拒绝。

确认 ID 需要沿用 Run/unit 身份并增加 `:cos:<sequence(event == cos_started)>`；generation 不进入幂等 ID。B1/B2/B3/Back 的 route operation 名相同但属于不同正常 unit，不能跨段碰撞；普通恢复同 unit 不重放已确认动作。

正常续段检查使用 `segment_complete(unit)`（或 summary 中同名字段）加实际根检查点及真静止，不能只看 active。advance 只在每段实际终点记录上一个完成 unit；unit_matches 是下一个阶段对应 unit，不要错误要求终点之后仍匹配旧 unit。正常段/恢复保留 phase、pending、request_seen、计数；不得通过清空 pending 恢复已发送但结果未知的跳跃/领取/guild 动作。

业务条件白名单新增：`/cave_of_separation/phase`、`active`、`pending`、`request_seen`、`unit_matches`，如以摘要检查续段还需 `segment_complete`。其余只作为诊断摘要：`pending_kind`、`started_cycles`、`completed_cycles`。已有 `/task_step`、`/inn_payment_pending`、`/special_dialogue_pending` 保持原契约。

## 待验证断言与具体风险

已创建测试源码但没有执行，状态一律 NOT_RUN。新增 native 入口复用现有 runtime_fixture、真实 Maa Pipeline/RunCoordinator/视觉/业务注册，不提供新运行解释器；因果 fake 仅在匹配的明确输入后推进帧。Python 独立 TestCase 不继承或重导出已有 WorkflowTests；完整六段通过组合复用其 execute 调用主代理已接入的 cave-of-separation 入口，使用私有显式 quest_catalog、with_state=True 和从旧源码/图列出的 extra_images。

已写断言包括：六图坐标/策略/预算；竞争帧停点零点击；AutoMove 后置停点；嵌套 Common 对话后停点；特殊意图确认与 bondmate_close 零点击；B3/Back 对同一 request 图片的不同冻结行为；未知/缺失/损坏停点；输入拒绝/UserStopped 保留意图；Fordraig 与 LBC 不同锚点 ROI；独立109输入的六段一周期及领取拒绝/停止不得进入下一段。Moving 中途才出现停点、完整双周期、慢因果及下列剩余矩阵尚未全部编写，不能把这些待验证项算作覆盖完成。

- 六段分别编译、注册完整、预算包含 boot 包装后<=1800秒；六份冻结策略正确，错序、缺 WvdVision、缺检查点、未知段/停点/策略、超过周期预算在输入前拒绝。基础58项目录和旧默认路线图逐字节保持原有语义。
- 以固定 Python 源位置独立给出全部坐标、入洞次序、10秒等待和返城 trace；完整6段一周期、连续2周期；住宿每周期恰一次，不受 ACTIVE_REST 或 REST_INTERVEL 跳过，套房配置沿用现有行为。
- ACTIVE_CSC开/关、快路径/慢路径；慢路径 GhostsOfYore / COS/ArnasPast，空因果 options 不自动启用任何新条目。跳跃已发送但后置未知时不得重发。
- 领取覆盖 COS/Okay 可见和 guildRequest 已可见两分支；原代码只是 find guildRequest，不点击它；最终找到 guild 后确实点击并返回 Inn，不把找到了按钮当完成整个周期。
- B1 takehimwithyou、B2中途 Ena、B3中途 requestwasfor，三个新帧边界分别确认；最后一个坐标尚未计数时到停点也允许子返回，反例为错误停点/未知页/提前返城/点数耗尽。停点不会触发 UserStopped，也不能点击停点或继续旧路线。
- 必测停点首次出现在 AutoMove 后置、Moving、Common 对话后的帧，而不是只用开场已是停点的简化夹具。B3开场遗留 Ena 需正常处理；Back开场 requestwasfor 需按新的冻结策略点击一次并承接后续路线。
- B1、回程必须全部点数和新帧最终位置；最终 `(660,919)` 如果实际/独立旧因果样本证明会直接退城、无法保留最后地图确认，应记录具体差异并补“已发送最后目标且退出”的真实因果回执，不能单凭 Inn、task_step==6 或子图 Completed 放行。当前实现对此保守拒绝，未获实机证据。
- PrepareEntry 使用现有 enter_dungeon 的地图/地下城/战斗确认；若独立离线样本证实 COSENT 后直接出现纯对话页而无这些标识，需在明确接线范围补入洞后置，不能将任意 blocking_screen 当已入洞。
- Stop、输入拒绝、识别 Error、帧过期、付款/对话/领取结果未确认、恢复新 generation 的正负链；保持已发副作用事实和真实静止证据。pending 不得被重启清除以换取成功。
- 真实素材资源闭包与别名/mod、缺失停点资源、同帧默认选项/停点竞争、不同Run/策略互不串扰；Fordraig有序两选项另由该代理专项验证。

未完成主代理接线与上述验证前，本项不能增加任务通过数，不能作为生产切换依据。
