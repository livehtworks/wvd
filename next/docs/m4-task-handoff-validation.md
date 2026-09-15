# M4.6 任务转交与长等待交接

## 当前结论

代码状态：**PARTIAL / FRAME_AGE_BLOCKED**。
统一构建通过，首轮8方法4通过4失败，设备 **NOT_USED**，`release_allowed=false`。
主代理构建日志为`m4-extensions-and-stages-build.log`，不包含实机或生产切换。
未解决任意 SDK/连接/清理阻塞取消、M3 资源和长期稳定性问题。

首轮证据为`m4-handoff-6ijr2s54`，产物SHA256为
`96ac9ed2b8894d694cffd0ed71af0f839e935baaf7057bce42e7909e20fe7fb1`，
已在重建前核对10份已有哈希记录。wait/stop-wait/wait-budget/money均在进入等待或转交前
因`LEAP_OBSERVATION_STALE`失败：未知计数5及跳跃图标确实命中，但同帧复核超过2秒。
四者均generation=1、Failed、静止且保存成功，backend_called=0；存在一次关闭门禁拒绝的
输入尝试，不能写成零尝试。缺少失败用例execution.json是旧夹具记录缺口，不事后伪补。
state/extensions为状态验证；late-connect证明限定阻塞的持有边界；storage-fail虽断言通过，
但该轮没有生成有效转交意图，仅覆盖存储失败拒绝，不算有效转交的存储负例已验。

定点修正一：未知分类的稳定页/基础覆盖层探针使用同步并行批次，仍按原顺序消费结果、
保持2000ms帧龄和Error语义。补暗灯独立未知分支及专属正常页的重新截图复核；
增强存储负例，必须先得到真实转交意图再验证拒绝。

修正一证据根`m4-handoff-wukgf_9a`：4通过、3失败、1错误。money子项首次通过，
旧Run保存后同一协调器启动Run=2，实际点击一次后UserStopped；重复poll返回同一Run且
不新建目录。三种等待仍在进入等待前帧过期；存储负例增强断言正确暴露意图为空，不能标通过。
修正一整批48份execution/11份哈希记录已在重建前核对，详见阶段汇总。
最后一轮定点修正将等价默认模板阈值统一到同帧memo，并同步计算三个独立面板反证；
保持原候选消费顺序，不合并不同ROI/阈值/预处理，专属有状态条件仍串行。待新产物复验，
若同一帧过期再次失败则按工作包保留阻断，不继续无限重跑。

最后一轮定点产物`007a7d6`：9方法7通过2失败（私有根`m4-handoff-_dnkioai`，
批日志`m4-007a7d6-vpn-handoff.log`）。7300秒的受控时钟五段等待首次完整通过，
最终generation=7、Completed、零普通输入，只有StopApplication/StartApplication两次
离线生命周期调用，真实静止并保存。money、有效意图后的存储失败、两周期状态、
原生连接迟到返回所有权，以及交接后按冻结配置重查VPN均通过。
stop-wait和wait-budget仍在generation=1产生意图前因LEAP_OBSERVATION_STALE失败，
intent=null、零底层输入、已静止并保存；不是已经进入等待后停止/预算行为被证明失败。
两个覆盖点仍未验证且达到本包有限修正边界，保留BLOCKED，不重复运行挑PASS。
用户已取消全游戏任务矩阵作为本轮前置，这不把本项公共帧龄缺口改成PASS。

正式`dungeon_route`未知分支已经使用`PipelineCompiler::unknown_leap`，发布器绑定
WvdUnknownLeap，并将固定等待节点纳入同一封存revision。专项fixture已改用正式编译器
和发布器，不再生成后手工篡改节点。它验证有限未知分流，不冒充完整原任务。
独立离线入口消费TaskHandoff，M1服务仍只读；HTTP/WS运行控制属于M5，本轮不接入。

## 已核对依据

- 工作包 `01_WORK_PACKAGE.md` 第14、17节；根 AGENTS、当前事实、执行注意项。
- 固定旧源 `legacy/src/script.py:2225-2245`：counter 从0起，第五次未知才进入此分支；
  先处理 RiseAgain/sandman，再判断 cursedWheel_timeLeap。ACTIVE_BEG_MONEY 为真发消息并退出；
  否则 Sleep(7300) 后 restartGame。
- `legacy/src/main.py:97-110`：只改现有 setting 的 FARM_TARGET 并重置下一任务计数，
  等旧线程退出再启动；没有重新选择7000G配置区段。
- 实际新链：RunCoordinator::collect_session -> finish -> 原子 result.json ->
  释放 DeviceLease/active=false -> TaskHandoff::poll -> publish_workflow(gold_income_cycle) ->
  **同一** RunCoordinator::start。quiescent 单独出现不能触发交接。

## 本轮实现

### 转7000G

- `WvdRunState::observe_unknown_leap` 核对 generation/frame 与至少5次未知，只记意图，
  意图不含新 request_id；重复观察不重复写入，旧代次拒绝。
- `tasks::observe_unknown_leap(Context&, parameters)` 使用正式 WvdVision 的 unknown_exhausted(max_tries=4)
  读取既有 unknown.window，再从同帧识别 cursedWheel_timeLeap。它不增加未知采样次数，
  不把 boolean-only unknown 观察变成输入许可；Error/过期明确失败。
- `freeze_handoff_source` 冻结当前任务和目录中的精确7000G定义、profile值哈希、33字段来源、
  选定区段及 legacy/passthrough 摘要。状态工厂核对来源，不能运行中改读 config/profile。
- `TaskHandoff::start_source` 是应用入口：给已有定义附加冻结来源后调用既有 start。
  `poll` 要求协调器 inactive、真正静止、保存成功，并核对原 run.json/result.json。
  只有 Interrupted/RECOVERY_REQUIRED 且末段原因 leap.unknown、意图为 turn_to_7000G 才生成请求。
- 新请求ID由来源摘要和意图身份确定；重复 poll 交给核心幂等表，不新增 runner。
  新 profile 只改 FARM_TARGET，保留已选配置，不重选7000G区段；来源链写入下一 run.json 的
  state_factory.parameters.handoff_parent，含旧实例/Run/意图/结果哈希/字段来源。
  不绑定 profile_store，不写用户配置。新统计与旧 Run 分开，旧结果不重置。
- 新7000G图由现有 gold_income_cycle 和 publish_workflow 真正生成；继承输入策略及显式下载
  策略（构造器默认禁止下载）。若来源具恢复策略，为新单图重建 Checkpoint -> Boot_Entry 映射，
  不继承 StageN 入口。服务应独占这个 TaskHandoff 对象的 start/poll/stop 生命周期。
- 幂等范围是同一存活协调器的现有请求表；没有增加跨服务重启的自动续跑/自动重放承诺。

### 7300秒恢复等待

- `recovery::LeapWait` 使用既有可注入 MonotonicClock，零起点有效，回退时钟拒绝。
  截止点固定相对于原观察：1460、2920、4380、5840、7300秒，段间清理/连接不重新起算。
- 等待属于原 Run 的恢复链，最多五个有界等待 Session，每段1500秒预算（1460+40秒余量）。
  保留原业务 unit/phase/pending；不消耗正常 continuation_units，不计 completed_business_units。
  段后 RequireRecovery(leap.wait_boundary) 表示同一次恢复尚未结束，不伪造正常任务完成。
- 每25ms检查协作取消，等待本身零截图/零输入/零设备生命周期调用。此界限不适用于
  Session创建前的连接、SDK运行/清理及磁盘阻塞，不能据此宣布任意阻塞可取消。
- boot::decide 在原副作用保护之后检查意图：转7000G不重启；等待未满只选等待入口；
  等满后才生成原应用/VPN/连接/实例恢复计划。原同次升级次数、Crash策略保留。
- 等待会话结束后恢复该业务 Session 原预算；真正游戏重启确认后才清除活动等待意图。
  恢复预算耗尽/连接失败/停止保留 Interrupted/Failed/UserStopped 等实际结果，不静默续跑。

## 集成契约

以下1至5、7已接入当前构建，等待原生结果；第6描述独立离线调用契约，
不是要求提前改造M1服务。任何调用者仍必须遵守这些来源和生命周期限制：

1. **生产未知分支**：在既有 unknown_frozen 采样之后、相应未知退出之前接 hook；保持原
   正常画面、RiseAgain/sandman 优先级和 MAX_TRY_LIMIT 顺序。为 PipelineCompiler 增加固定
   `WvdUnknownLeap` 准入及对应构造入口（不开放任意 CustomAction）。其 true 后继为
   RequireRecovery(reason=`leap.unknown`)，false/NoHit 后继继续原未知链；异常不能当 NoHit 吞掉。
   绑定为 `tasks::unknown_leap_binding()`，状态工厂已注册实现，不要重复注册。
2. **图片闭包**：为 hook 收集精确 `cursedWheel_timeLeap.png`，并保留 unknown 识别所需
   当前分类资源；这是隐式正式依赖，不能只在测试中补图。
3. **publisher opt-in**：共同发布内部加入一次 `recovery::leap_wait_nodes()`，建议保存为
   `pipeline/leap-wait.json`，将节点内容纳入 revision 身份及最终文件manifest。
   必须保留顶层 `LeapWait_Entry` / `LeapWait_Boundary`，不经过 scoped_stage 改名，
   不注入 child reset_hit_counts。它是同一冻结包的恢复入口，不能另建一个不同revision等待包。
4. **Session/预算**：所有 initial/continuation 定义组装完成后、冻结前调用
   `recovery::bind_leap_wait(run)`。它绑定 WvdLeapWait 和各段 resume_time_limit_ms，
   在3至11的既有恢复预算上加5，总数仍<=16。publisher 不要预先再绑定同名 WvdLeapWait；
   该函数拒绝重复绑定，失败不会留下半更新定义。单段原预算仍须<=1800秒。
5. **多图恢复映射**：发布后把每个 Session 的 checkpoint_node 映射到其实际已发布 boot入口：
   `run.recover->parameters["boot_entries"][sessions[i].checkpoint_node] = "StageN_Boot_Entry"`。
   使用 publisher 已知的命名空间事实，不从运行中可编辑配置推断。
   `boot.cpp` 已消费该映射；存在映射但缺键拒绝，缺映射仅接受原单图 `Checkpoint`。
   等待 Session 保留当前 checkpoint，因此能回到原阶段，而不是第一阶段。
6. **应用命令入口**：持有原协调器与封存注册表/资源，构造 TaskHandoff；原启动调用
   start_source(run, imported_profile, selected_task, frozen_catalog, offline_backend)，
   服务循环 poll(全新编译目录)，停止用 request_stop。UI只收状态，不另调另一个 runner。
   现有真实后端权限不变，该入口本轮显式只接受 offline backend。
7. **CMake/工具**：主代理已加入两个新cpp及 `test_m4_handoff`（foreach handoff fordraig）；
   本代理只读核对，没有改 CMake。正式工具发现该专项测试后，等待全量整合构建完成、
   核对同一EXE哈希，再串行执行；本次没有代执行。

## 同轮追加：Fordraig/COS

按用户追加授权，已在共享状态接入两个已落盘 domain：

- Fordraig 16个明确事件、COS 17个明确事件同时进入 confirm_event 和编译器事件白名单。
  未知同前缀事件仍明确拒绝；确认ID保留 Run/unit 并增加各自周期序号，不加入 generation/frame。
- 正常续段分别检查 continuation_ready(previous_unit) 与 segment_complete(previous_unit)，
  并要求 unit 顺序递增。恢复保留 phase、pending、周期与路线事实。
- 各周期只在 started 增加一次旧尝试/dungeons/lap口径；Fordraig领取检查
  featured visits_completed 的一次增量（不要求发生新的选取），COS住宿使用真实已完成回执。
  路线/返城确认拒绝未结算战斗、宝箱、住宿、特殊对话/领取副作用。
- Fordraig 非Boss Auto仅派生到摘要，Boss恢复原策略实际 automatic 值，不改profile/策略行。
- 摘要与实际图消费的业务条件白名单已增加；boot及handoff均拒绝两域pending。
- 按Copernicus接口接 boot/common 停点：入口先于普通/特殊对话；根作用域动作/子调用后继
  先观察停点，不向子图内部插跨作用域边。common正常子返回；boot分别以稳定图片确认重启后
  回原任务入口，不写UserStopped，也不直接发COS阶段/根完成事件。冻结dialogue_policy保留。
  停点确认与boolean-only boot_ready分开，避免any组合稀释确认资格。

其余 domain/tasks 来源与完整矩阵见 `m4-fordraig-validation.md`、
`m4-cave-of-separation-validation.md`。多图publish/CLI/原native workflow fixture、视觉停点、
完整任务及配置/mod回归由主代理/对应代理继续负责；本文件不把这些项目改成PASS。

## 验证记录与限制

- 已执行：Python `ast.parse` 只解析 `tests/m4/test_handoff.py`，成功；未import测试。
- 已执行：本代理修改的六个既有C++文件 `git diff --check`，无空白错误，仅Git CRLF提示。
- 未执行：C++编译、原生程序、unittest、Maa运行、设备测试、性能/资源验证。
- 专属测试写有8方法：6个真实原生运行场景（转交首输入、五段等待后重启、等待中取消、
  等待预算耗尽、结果保存失败、连接STOP_TIMEOUT保留所有权）和2个支持性状态组
  （第五次/幂等/时钟；Fordraig/COS各两周期及pending恢复保护）。全部 **NOT_RUN**。
- 原生 fixture 使用正式 WvdVision/状态工厂/恢复策略/TaskHandoff/RunCoordinator，
  只有图的unknown外层选路和离线设备属于夹具；长时间仅推进注入业务时钟。
  7000G只设计验证实际下一Run与首个受控输入后停止，**不是完整7000G收益流程验证**。
- 状态组不冒充Maa/真实新帧/设备或完整路线证据；外层180秒watchdog只会使测试失败，
  不能当生产取消方案。尚未证明帧龄、资源闭包、全正常任务恢复矩阵和任意阻塞取消。

## 文件释放

本代理交付完成后释放 `state.hpp/.cpp`、`state_factory.cpp`、`recovery/boot.cpp`、
`business_condition.hpp`、`tasks/pipeline_compiler.cpp`，以及本报告和新专属模块/测试。
保留开工时的Repel及后续其它代理修改；本轮未改现有test_m4_workflow.cpp/test_workflow.py、
vision或dungeon_route。当前滚动事实与其余专题报告由主代理合并刷新。
