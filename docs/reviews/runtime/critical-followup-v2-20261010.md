# WVD 关键代码继续修复与 Codex 承接报告 · V2

基线：`livehtworks/wvd`，`agent/local-stability-notes`，`d03a7657b9ce5266842b26e3badbe08390e83d06`。日期：2026-10-10。本报告中的源码修复是可审查、可应用的未提交候选；没有替用户 commit/push、部署、操作设备、启动循环或开启新采集。远端复核回执在 `reference/REMOTE_RECHECK.json`，不冒充用户本地工作树检查。

## 一、结论

继续审查找到了可复现的代码错误，已经有“修正前失败、修正后通过”的定向证据。最关键的是运行结束后的 Session 所有权与输入事实接管，以及下载禁止规则在实际调用范围内的持续性。它们说明此前的问题确实包含调用链和异常收尾错误，不能统一归咎于图片识别。

同时，历史的 45.1 MiB 收尾增长仍然没有合格的存活分配栈净增归因。本轮没有制造新的真实内存数据，也没有用对象计数或受控测试冒充泄漏根因。`RESOURCE_UNRESOLVED` 保持。旧 43 项逐项承接，所有条目都保留未完成的真实边界，没有批量写成已关闭。

本轮交付以最终 `freeze04` 为源码身份：Session 收尾 13 例、直接停止结构 49 例、救人与停止 44 例、执行器 19 组及共享处理的工厂/运行/结构验证均有明确回执。下载范围专项还补了真实 Party/Inn 的未决输入与共享失败出口反例。受影响文件停止编辑后才重新编译；保留失败的早期候选与夹具，不用旧二进制替代最终候选。

## 二、Session 已结束，但异常诊断让它留在 Coordinator 中

### 原来的链条

`NativeRunCoordinator::drive()` 中，`session->run()` 返回后先调用资源统计、所有权统计等诊断，再清理 `session_`、接收输入计数与结果。诊断内部可能分配。若此时抛异常，外层还能结束 worker 并保存终态，Coordinator 却继续持有已经结束的 Session。

这个 Session 的 ports 和操作回调仍借用 `drive()` 内已经退出生命周期的业务对象。后续清理责任、输入统计和原停止原因也受影响。受控反例 `ownership-failed-unknown` 中，旧实现实际向后端提交一次，终态输入计数却为 0，原因变成 `std::bad_alloc`，join 后 Session live=1。

### 本次修改

修改 `next/native/runtime/native_run_coordinator.cpp`，保持原 Coordinator、Session 和 FlowExecutor 主链。

1. 清理 guard 直接持有闭包，建立 guard 不再依赖 `std::function` 分配。先建立清理责任，再发布 `session_`。
2. `run()` 返回后，先接管实际输入清理状态、计数和完整未决账目。pending 原数组先 swap 到控制快照，再创建诊断副本，副本分配失败也不能丢掉原输入事实。
3. 先解除 Coordinator 对已结束 Session 的发布引用，再处理可选诊断。资源/所有权诊断异常独立记为诊断不完整，不替换原业务原因，不触发新的恢复动作。
4. 保持已有输入最终释放与终态保存条件。清理失败仍 `quiescent=false`，仍拒绝复用；终态保存失败仍不发布 `run.terminal`。没有清 pending 或假报成功。

同一夹具只替换 Coordinator 翻译单元：旧实现 6/13 通过，新实现 13/13、257 条断言通过。上面的 Unknown 反例在新实现中仍只有一次后端提交，计数为 1，原 `MULTIPLE_DEATH_MANUAL` 和整份 pending 保留，Session live=0，诊断失败另记。

每个确认清理的场景还用同一 Coordinator 启动独立的无输入运行，核对没有重放上一笔输入。明细逐字段检查原 source_path、frame、epoch、attempts、设备/连接/实例创建身份、采集和提交时间。完整证据：`validation/v2/control-cleanup/CHANGE_SUMMARY.md`、`candidate02/RESULT.json` 和 `candidate02/{before,after}/`。

**边界：** 实际编译 Coordinator、Session、NativeFlowPorts、FlowExecutor 和全文 NativeInputGate；EventJournal 等片段从生产文件逐字提取。Recognition Service 的结果/故障、Windows 平台、RunStore 持久化是受控适配。没有执行 Application API、完整 Windows Service/RunStore 或 Windows `test_native_coordinator` 整目标。停止线程临时持有 Session 副本的所有交错、run 完整结果返回前的所有分配失败，仍要按实际 owner 继续验收。本证据不能说明这条异常路径曾在那 34 轮发生，也不能归因 45.1 MiB。

## 三、下载禁止参数存在，但后续步骤被父处理器接管

### 查实的三个问题

原代码有多条 `allow_download` 调用边没有继续传参。部分内部函数默认允许，导致上层的 false 丢失。本轮沿现有工厂参数补齐 19 条调用边，没有新增用户配置。另有 `dungeon_route(false)` 在创建 `Entry` 前绑定事件作用域，实际构造报 `COMPILE_EVENT_SCOPE_INVALID`；绑定顺序已修正。

继续做运行对照时，查出本轮早期、尚未交付的 Entry-only 修补不完整：“传参正确”仍不够。已有 lowering 只有在 `definition.checks` 存在时才把 Entry 的规则提升为 Definition 规则；没有 checks 的工厂，Entry 规则只属于 Entry 这一步。纯 `append` 还会把子图展开进父 Definition：即使复制了带前缀的 checks，也未必生成独立运行帧。

受控父流程声明了有效的允许下载规则，实际进入 `travel_world(false)` 的后续 `Locate` 后才注入下载提示。在这份尚未交付的 Entry-only 候选下，两种语言都出现：子流程转向 DownloadDenied，事件却选择父 `ParentDownload_Entry`，实际提交一次下载输入，再报告 `boot.download_permission_missing`。这不是把中间候选冒充 d03 实机证据；完整反例保存在 `validation/v2/shared-handlers/scope-01/`。

### 修正范围的做法

在既有 PipelineCompiler 增加 `default_body_event()`，在 `finish()` 生成 interruption、Checkpoint、Terminal 和 RecoveryRequired 后，把缺失的默认规则写入真正属于同一流程的步骤。范围遍历与 native 编译一致：走 next、on_error 和 Call handoffs，覆盖内联 Party/Inn，保留独立 Call target 和 handler definition 边界。规则的 source_node 逐节点改写，Input 生成的 Await 继续沿原 lowering 继承对应规则。

已有显式同 ID 规则或 disabled 优先，其他规则不变。为了保留现有共享失败节点合同，按实际根和 handler 的可达关系识别多个 Definition 共享的无后继 RequireRecovery：这些节点只禁用该下载事件，保留原失败原因；专用 DownloadDenied 仍走拒绝处理器。这样不会形成处理器经共享失败出口再调用自身的回边。

没有用强加 check_policy 改变继承白名单、输入保护或业务阶段，也没有多包一层 Call 来规避范围问题。策略只在本流程活动范围有效，离开 false 子流程后，原父范围继续按自己的规则运行。新增规则的复制发生在编译期，属于正确性修正，不能称为内存优化。

这项修复沿已有编译器与事件系统进行，没有新增执行器、watchdog 或全局设置。`Application::compile_task_graph()`、作者 task_stage 和根恢复包装沿用原默认允许行为。本批动态与结构证据覆盖 world/city/entry/departure/dungeon/time 等实际子流程的范围，不代表所有顶层任务的整段合同已经完整。尤其 `gold_income_cycle(false)` 的最外阶段编排仍有 51 个步骤未挂新的拒绝默认规则；false 已传入 time/travel 子工厂，但不能由“整图没有直接下载 Input”推导父处理器在顶层也绝不会接管。这个顶层缺口明确交 RA01/RA04，继续核实际父作用域和原参数职责。没有宣称实现可抵消任意矛盾父子参数的产品级权限门禁。

## 四、直接人工停止的结构矛盾在编译时拒绝

原实现会接受“直接人工停止”同时还带 handler、重规划或重启元数据的组合，部分字段在 lowering 中被静默丢掉。运行时只判断部分字段还不够：作者声明、继承与节点覆盖的合并已经可能改变规则含义。

现在在三层检查这个合同：

| 层级 | 检查内容 | 错误前缀 |
| --- | --- | --- |
| 作者声明、继承与合并覆盖 | 停止规则不能藏 arguments、reobserve/replan 或非空 nested handler | `EVENT_BLOCKED_INVALID` |
| Pipeline 编译描述器，数组和对象 scope | external_blocked 不能带 entry、local_counters、resume、重启指令；reason 必须完整 | `COMPILE_EVENT_STOP_INVALID` |
| 原生 Definition/Step 事件 | handler_definition、重启、replan、resume_guard 等不能与直接停止混合 | `FLOW_EVENT_STOP_INVALID` |

错误包括源流程/节点或 source_path、事件 ID 和违规字段。18 个正常对照保留直接停止、合法 handler、正常重规划/重启和显式完整重声明。31 个混合非法组合在旧实现全被接受，新实现全拒绝，同一测试头合计 49/49。

修改集中在 `authoring/event_policy.hpp`、`tasks/pipeline_compiler.cpp` 与 `workflow/program.cpp`；测试接入现有 `test_native_author --event-stop-contract`。这不是“按事件名字猜必需职责”的新机制。完整调用职责和保存流程实际 revision/CAS 仍由 RA01 处理。

## 五、共享已有页面动作，保留调用者的完成条件

新增 `next/native/games/wvd/recovery/page_actions.hpp`，由当前 PipelineCompiler 和公共作者编译路径消费。共享的是同一页面的动作配方，调用者继续持有完整场景、后置条件、下一步、等待与次数预算。

| 处理职责 | 本批处理 | 保留的差异 |
| --- | --- | --- |
| 下载确认 | 五处实现复用，包括精确公共作者下载合同 | 两语、网络/Pause blocker、原 retry、后置条件与权限 |
| Pause 恢复 | 两处六段操作复用 | 事件路径原连续限制 5，Boot 原限制 6；坐标、间隔、预算不变 |
| 普通剧情继续 | 六处复用完整剧情页面与箭头配方 | 每个调用者自己的 delay、post、next 和次数 |
| 补给提示继续 | 两处复用已有提示箭头动作 | 调用者完整 scene 与各自出口 |
| 角色页关闭 | 两处复用同一页面与固定动作 | 完整页面身份与原 retry |
| Auto 技能弹窗清理 | 两处共享既有关闭/取消/BACK 顺序 | 战斗+技能弹窗前提、偏移、原 retry 与结果分别保留 |

公共作者下载只有 `scene == target`、`post == absent(target)`、零偏移等完整旧合同吻合时才走共享配方；非零偏移或复合场景仍走原通用分支。没有按节点名字就批准输入。

旅店按钮退出与付款后的 BACK 是两种业务情境，本轮没有强行合并。战斗的 UnownedDetail BACK 比 Auto 弹窗清理更窄，也继续保留。它们列入 RA04，先用完整调用者证据证明可共享的范围。

原生公会 `CloseReveal` 与公共 JSON 补上已有正确分支使用的 `!combat_active`，保持 reveal 的完整身份，避免仅在能看到关闭目标时继续关闭。两个作者副本同步。它是缺少战斗排除条件的源码修正，缺事故原帧时仍不能声称误判的现场原因已经证明。

## 六、第一版关键修复继续保留

累计补丁包含第一版的救人职责传播、Common/重连多人停止、原单人中心动作、Unknown 全栈禁止新输入、统一完整场景与目标许可、公共下载 retry、Pause 返回城市、三个有效事件表消费端取消深拷贝，以及资源清单修正。

第一版已交付 ZIP 和源码工作树本轮保持原字节。V2 增量补丁以该精确候选为基线，不以“看起来差不多的第一版”判断可应用。第一版原验证输出放在 `validation/v1/`，没有把它们改日期冒充本轮重跑。当前实际输入等价检查列在 `validation/v2/FINAL_INPUT_EQUIVALENCE.json`。

### 最终验证清单

| 验证组 | 实际结果 | 证据与限定 |
| --- | --- | --- |
| 直接停止三层结构 | 49/49；31非法拒绝、18合法对照 | `validation/v2/call-contracts/candidate03/comparison.json`；8个TU干净重编，同头的V1反例身份重新核验 |
| Coordinator 收尾 | 新13/13、257断言，旧6/13 | `validation/v2/control-cleanup/candidate02/RESULT.json`；实际35个仓库编译依赖与最终源码一致，受控Service/平台/持久化 |
| FlowExecutor 回归 | 19/19组；Unknown人工停止组含21例 | `validation/v2/runtime/candidate01/summary.json`；实际15个编译依赖最终字节一致，不伪装成重新运行 |
| 救人、多人停止与Pause | 44/44＝17+17+9+1 | `validation/v2/rescue/candidate02/RESULT.json`；30个TU全重编，单人只证明第一笔原中心动作 |
| 共享工厂与运行 | 71个工厂实际native compile、30/30运行 | `validation/v2/shared-handlers/after-03/run/summary.json`；V1为69成功及2个预期旧构造失败，同头按版本记录行为 |
| 默认作用域结构 | 11/11断言 | `validation/v2/shared-handlers/after-03/run/default-scope-contracts.json`；新API结构验证，V1没有该API，不登记V1通过 |
| Party/Inn与共享失败专项 | 3/3 | `validation/v2/shared-handlers/inline-01/`；保留原Accepted pending、无新增下载输入；不等于执行整个Departure或真实Service |
| 传参接口 | 13个cpp语法编译、19条调用边核对 | `validation/v2/shared-handlers/propagation-check/results.json`；不将语法编译算作任务实跑 |
| 下载同页补试 | 10/10，两种语言 | `validation/v2/download/candidate02/source_verification.json`；11个TU全重编，网络/Pause/消失/Unknown不误补 |
| 门禁95项 | 第一版95项和两语AutoOff证据原样保留 | `validation/v1/gate/`；没有改写为本轮95项重跑，V2另在实际全文Gate的13例受控组合取证 |
| 包校验器 | 合成17/17 | `validation/v2/package-verifier/FINAL_RECEIPT.json`；最终实际补丁/ZIP结果另见包回执 |

这些是不同层级的例、组和编译检查，不能直接相加声称一个“全部产品测试通过”的总数。完整动作节点对照有168份逐字段相同；工厂完整图45份相同、24份为预期差异，另2份旧构造失败在新版成功。差异明细保留在 `validation/v2/shared-handlers/comparison-final/`。

## 七、资源与构建验证边界

两份 public-flows JSON 相同，SHA256 为 `a500e990256ff8af8de798da9bca19722c65c00268c7f8e1c0ba51c10281ed0f`，116,803 字节。沿现有生成器更新 manifest：只变更该条载荷身份及相应 revision，未改无关资源。新 revision 为 `46e7346025d68a9cb667652115afac37bc770d4a83570b4bf5c29490ed6e7d27`。

这是对当前可取得元数据的验证。manifest 声明 591 项，本环境只有 2 项载荷，缺 589 项；允许缺失的元数据校验通过，完整校验按预期因缺 `image/7000G/icantagreewithU.png` 失败。没有宣称完整资源包可发布。证据：`validation/v2/pack/candidate01/manifest-sync-receipt.json`。

正式六模块运行器只验证了计划选择：app 12、devices 3、recognition 13、runtime 19；tools/web 有其独立工具执行逻辑，不能把 native 项数为 0 理解成无需执行。当前环境没有 cmake、pwsh、dotnet 或完整 Windows SDK/OpenCV 产品依赖，因此没有完整 Windows 构建、正式六模块全套、真实 Service 或前端组合通过结论。

## 八、内存问题的下一步已经具体化

`d03a765` 的状态查询已经走 `latest_node`，不能继续拿更早的 `run_status → EventJournal::read` 整环复制当作当前源码路径。若当前采集栈仍指向旧链，先核对真实 EXE/PDB 与对应调用者，区分状态 API、事件 API 和外部监控。

已有 HeapSnapshots wrapper 的当前端点合同支持合格的 acknowledged worker_joined 成对端点，或合格的 batch_payloads_released 成对端点，不能混用。heap_analyzer README 还写成只允许后一种，本轮已纠正说明，没有新增采集代码或开启采集。

RA07 要求先做离线工具自检，证明端点身份、释放范围、采集开关、符号、丢事件和输出限制可验证；现场负载由用户另行决定执行时机。取得合格 A/B 后，按稳定分配栈输出正负齐全的存活字节/块数净增，核对总量守恒，再追正增头部的 owner 和应释放入口。已记录 VA 不等于当前聚合器已分析 VA，堆快照也不提供 free 栈。PrivateUsage、OCR/Session 对象计数、一次回落都不能替代这些证据。

保留有界方案：最多 512 MiB 采集量、20 分钟总期限、128 MiB 分析输出及子进程内存限制；任何资格失败先修工具，不扩大全系统 VA 或无限导出。分析成功、可比较、根因已归属是三个不同状态。

## 九、给 Codex 的剩余工作包

| 工作包 | 剩余关键工作 | 完成所需证据 |
| --- | --- | --- |
| RA01 | 实际调用边职责与保存流程迁移 | 真实 Definition/append owner、父事件/继承/局部覆盖、返回边、原 revision 的精确 CAS |
| RA02 | Application 与控制链完整停止/提交边界 | queue/start/watch/stop、回执落盘、取消竞态，原 Unknown 账目保持 |
| RA03 | 真实 Service 的输入许可验证 | 完整请求和事故原帧；同帧 scene/target/权限到实际 Gate 的证据 |
| RA04 | 剩余有语义差异的共享处理与现场问题 | 各调用者完整动作/后置/等待合同，Inn 与 UnownedDetail 不盲目并轨 |
| RA05 | Windows、正式模块与最终候选身份 | 实际源码/资源/EXE/PDB、完整包、真实组合和保存流程的分层回执 |
| RA06 | 当前文档、能力声明和 CI 对齐 | 正式结果与声明逐项相符，历史授权和旧缺陷不覆盖当前状态 |
| RA07 | 内存存活分配净增归因 | 合格同阶段端点、净增表、守恒、owner/释放责任，缺项明确未归因 |
| RA08 | 旧 43 项剩余所有权、性能、存储与崩溃边界 | HANDLE/forward 异常；真实 OCR 成本；面具面板证据；配额事务；自然 renderer dump |

恢复 46.35 秒与 1199 次比对、战斗入口约 95 秒、面具打开无进展、真实 OCR 初始化成本、自然模拟器崩溃，都没有在本轮被实机重新归因。用户原报告中的系统提交 99.3% 说明压力，不能仅凭这个数字断言 renderer 或设备故障的原因。

旧 43 项保留在 `REMAINING_43.md/json`，V2 新发现单列 `V2_FINDINGS.json`。每项区分历史状态、本包已经做了什么、当前剩余风险和下个工作包；没有因增加测试就把所有旧项标成完成。

## 十、包怎么用、怎么判断证据可信

先读 `CODEX_START_HERE.md` 与目标仓库现行说明。精确 d03 基线用累计补丁；精确第一版候选用增量补丁；相关文件已有修改则逐项合并。两份补丁二选一。`PATCH_MANIFEST.json` 给出全部受影响文件的 before/after SHA256 与 Git blob；`VERIFY_PACKAGE.py` 只读核验包和指定仓库基线，不替用户应用、清理或提交。

本包的补丁验证在新的受影响文件投影中执行 `git apply --check`、应用、候选逐字节核对、逆向检查和逆向恢复，不能替代用户真实仓库合并结果。`changed-files/` 是完整可审查文件，不是覆盖用户工作树的脚本。

原始编译命令、依赖、测试输出、前后源码身份和早期失败均保留。两次中间对象异常只有“编译回执非空 ELF，链接前读为 0 字节”的证据，原因未确定；损坏对象重新编译，其它复用对象必须同时匹配记录的 hash、ELF 和全部实际非系统依赖。共享测试早期临时 JSON 子对象生命周期错误已在夹具中纠正，再用同一修正头对两个版本运行。Coordinator 首轮 JSON source_path 夹具错误也单独留档。

大 JSON 无损 gzip 后打包，映射保留原 SHA 和压缩件 SHA。没有分发构建的可执行文件、对象文件、用户配置、`.vscode` 或用户运行数据。最终包内 SHA 清单与 ZIP 解压字节校验共同对应交付；任何未执行的真实层级仍明确登记。

## 十一、2026-10-11 Windows承接

前十节保留外部包的历史结果，本节才是本机实际承接；没有把Linux结果改名为Windows通过。

- 精确d03基线应用累计补丁，不叠加增量。ZIP SHA256为`6c4d3d97b1863864952a0c4c41aca231137910e2a70d3586bc965f9682aaf88c`；2204项包索引、64份基线Git blob、应用后64份候选blob均核对。Windows Python3.14的ctime差异在独立校验器适配，原验证失败与原包字节保留。
- 另补7000G根业务体的禁下载范围。实际允许父在Task_Phase0之后出现提示的反例：禁止路径选择原拒绝处理器且零输入，允许路径由原父处理器提交一次；不伪称完整7000G游戏任务完成。两语各有正反对照。
- 在原编译器声明独立事件职责，六个既有工厂及四份公共流程声明单人救人/多人停止。沿实际Call、父事件、inherit、局部覆盖、disabled、append owner及返回边审查；截断内层继承、后续节点禁用的反例拒绝，独立无提供者工厂明确undefined，不虚报完整。真实巨人图审查通过。校验仅发生在编译期，没有运行时扫描器或新的次数/期限。
- Windows受影响六模块最终回执`next/.local/followup-v2-20261011/contracts-final-7/receipt.json`为complete=true；输入`cee7e04a7a94de95a8f42801e66d6bf44c17015c9cc02c6308967dfd74dd6c54`、1299项。runtime 19、devices 3、recognition 13、app 14个原生命令，另11项工具/前端检查；不能相加当作游戏通过或全部43项关闭。共享专项包含75个真实工厂构造/原生编译和34个受控运行场景；真实Windows Coordinator基本收尾、清理未确认、延后恢复取消、取图取消及logging-boundary已运行。
- 原帧组合执行真实Service→NativeFlowPorts→NativeInputGate和原救人FlowExecutor，只有设备传输隔离。救人原帧首个输入为(450,800)，技能详情/白色Auto战斗原帧均不授权救人、AutoOff或普通角色X。原图不修改；三个原帧SHA及完整请求、证据、账目在recorded-results-accepted。仅证明原帧/首输入，不是现场救人完成，也没有英文AutoOn正帧。
- 591项实际资源载荷SHA/大小已完整核对，锁定OCR模型用于原帧Service；原包“缺589项”的环境边界保留为历史，不继续当作本机缺图事实。Windows文本EOL失配按规范字节核对后经原生成事务修正，没有放宽资源租约或哈希。
- 所有初次失败保留：对象节数C1128、MSVC接口/JSON类型、资源租约遗漏额外成员、Windows计时前提、Handoff与PublicStepScope装配、直接Fail保留正常后继。修正测试必须保持核心断言和真实生产合同，不修改生产策略迎合夹具。用户`.vscode/`未动。

用户随后授权部署空闲服务及精确迁移五份保存流程；没有授权启动模拟器、游戏、循环或新采集。五份CAS及回读已完成，原始字节备份保留在saved-plan/before，迁移回执saved-migration-result.json记录原/新revision、SHA及字段变化；profile SHA仍为`3e873d08fb9135f558c3e3348b3bfffff3b73555c63a9176cc75cff274ccb87c`。首次字段范围校验因对象内部叶子展开拒绝，确认没有写入后按声明字段与精确内容验证修正，失败日志保留。

正式数据只读复制25份保存定义，其中20份公共定义由实际PublicFlowLibrary消费；八种受控巨人整链场景均Completed、task_step=2、bounty_cycle=1、pending=0。原始保存布局/参数与哈希回核，五份修改均在该库中；不把其余五份用户流程说成已经完整执行。首次夹具因只覆盖复合条件而无法覆盖生产确认序列化的等价压平结果失败；改为给已有语义场景注入实际条件叶子，仍隔离视觉，不改生产编译器/确认条件/业务结果。八场景不是游戏八轮。

这一步只改变了`next/tests/native/giant_linkage_cases.hpp`。将该文件替换为原HEAD字节重新计算全产品输入，精确重建contracts-final-7的旧hash；其余产品文件不变，runtime/devices/HEAD及堆分析器二进制另核。受影响app/recognition在contracts-final-8独立重验，27个原生命令通过；其余四模块沿用已冻结的逐模块证据，不把全部60命令重复重跑当作新增覆盖。最终当前输入`134747d460b9bc61c68a19422b4f6472acf92cdab3a4ac5e25ed7725d388487e`/1299项；对应合并回执acceptance.json明确work_package_complete=false。CI属性文件变更改为明确选全模块，远端CI仍未执行。

首次空闲部署实例`5473CCC8-F76A-45FF-8356-A9A693BD8F02`/pid27896，实际为Idle、busy=false、quiescent=true、run_id=0、connected=false；已完成只读HeapSnapshots Preflight，EXE/PDB GUID `f75103ce-903b-4757-b8f3-dd20d5e78eac`、Age121匹配，rounds_submitted/started=0。这里只证明身份/工具准备，不证明分配端点、符号栈或泄漏根因。此实例在最终测试源身份更新后经原manage_service空闲停止，最终冻结包重新部署回执另列于下方。内存保持`RESOURCE_UNRESOLVED`。

### 最终部署与恢复依据

- 正式`tools/build.py`最终构建/打包退出0，candidate Validate/Deploy经原管理入口。实例`17A75D99-780C-4F65-9BE1-619930A4691A`、pid52260、17654；再次确认Idle/busy=false/quiescent=true/run_id=0/connected=false。正式profile及25份保存定义均与验收输入回核一致；没有重复CAS、游戏输入或循环请求。
- 最终产品输入`134747d460b9bc61c68a19422b4f6472acf92cdab3a4ac5e25ed7725d388487e`/1299项；源码基线HEAD仍d03、实际工作树候选未提交。EXE SHA256 `6f1d99c7940b9cd410bb1f065d2b1b3061064b3f4593277d79d305821b86a983`；pack revision `19750612e30ebb27e08675801066cc7299012faaab060185efa45981c908ffdf`。
- 最终只读Preflight complete=true，EXE/PDB GUID `f75103ce-903b-4757-b8f3-dd20d5e78eac`/Age122匹配，PDB SHA256 `e74bfe92185758aa8b60973a0ca5844bd15c0fc46666fba6d4573450fa9d22db`。新的链接身份与第一次Age121分开；没有把同名PDB当作同一身份。采集提交/启动轮数均0，没有Probe/Collect/Monitor、ETL、快照启用或游戏采样。
- 证据根为`next/.local/followup-v2-20261011/`：acceptance.json、contracts-final-7/8、recorded-results-accepted、saved-migration-result.json、saved-acceptance-results-3、product-build-final.log、deploy-idle-final.log、memory-identity-preflight-final。首次失败全保留。冻结交付状态保持BUILT_NOT_GAME_ACCEPTED，长期稳定及内存归因不因部署改为通过。
- 整体人工恢复先按data-root/端口/实例身份通过manage_service停止空闲服务；使用已保留的完整previous候选并先Validate。五份旧workflow原字节在saved-plan/before，恢复前读取当前revision并人工核对是否有后续用户编辑，按原Repository CAS精确恢复，不能盲覆盖或自动切旧链。profile没有改动，不重置它。撤回的fedfbf5目录禁止作为恢复候选。

### 仍未闭合的验收

| 工作包 | 已补证据 | 仍缺证据 |
| --- | --- | --- |
| RA01 | 顶层禁下载、独立职责、真实Call/append及巨人图；五份CAS/回读、保存库消费与受控整链 | 全部业务调用职责及实际现场接受仍不能由已声明六工厂/四公共流程推导 |
| RA02 | 三层停止合同、Unknown/祖先保护、真实Windows Coordinator与现有Application合同 | 所有早期分配/停止线程交错、提交与watcher竞态、终态写入阻塞及真实Service取消组合未逐一故障注入 |
| RA03 | 三个未修改原帧的完整请求、Service/Gate与救人首输入 | 英文AutoOn正帧、公会reveal相邻过渡/Unknown原帧及完整现场救人结果 |
| RA04 | 既有共享动作、7000G权限根范围、调用者原出口保留 | 英文网络Retry阈值/点位差异原帧；Inn/BACK及UnownedDetail语义差异不盲目合并 |
| RA05 | 六模块Windows回执、591载荷与OCR锁定；正式构建及空闲EXE/PDB身份已核，最终同源包再冻结 | SDK逐故障与所有最小builtin实际消费仍缺完整验收，不以完整包掩盖最小闭包 |
| RA06 | 核心合同、索引勘误、59项目录/休眠工厂承接及本机模块选择 | 远端CI未运行，不写通过 |
| RA07 | 端点、屏障、限额、聚合器离线契约 | 没有授权的新A/B负载；尚无同阶段存活净增/守恒/owner归因，不能关闭内存问题 |
| RA08 | 当前模块重新验收及原帧证据补充 | HANDLE/forward逐故障、HTTP/Vue换轮竞态、复杂OCR实际成本、空间抢占、面具失败原面板及自然renderer dump/厂商契约 |

这些缺口不是部署失败或无限续跑的理由；空闲候选可以交付，但工作包不能因此写成所有验收已完成或长期稳定已通过。
