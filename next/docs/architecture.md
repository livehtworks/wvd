# 当前架构

## 唯一执行链

```text
Vue 工作台 -> 同源 HTTP API -> Application
  -> ProfileStore / WorkflowRepository / 资源快照
  -> 作者定义或 WVD 原生任务生成器 -> 发布期编译为 FlowProgram
  -> NativeRunCoordinator (一个 Run、一个设备租约、一个工作线程)
  -> NativeExecutionSession -> FlowExecutor (唯一节点/调用/事件栈)
       -> RecognitionService -> OpenCV / ONNX Runtime CPU
       -> WVD 业务状态和原子操作
       -> NativeInputGate -> DeviceSession
            -> MuMu 只读 CaptureHost，失败时指定设备 ADB 截图
            -> 指定设备 ADB 查询/应用及 VPN 生命周期
            -> scrcpy 锁版控制通道发送输入
```

`contracts/` 只保存中立值和结果；`workflow/` 保存强类型执行表示；`runtime/` 持有推进、事件、回执、停止和终态；`games/wvd/` 拥有任务规则和业务事实；`devices/` 独占设备 I/O；`app/` 是产品装配者。HTTP 线程不执行识图或任务节点，Vue 不持有设备或业务状态。CaptureHost 只隔离不可协作取消的厂商截图调用，不参与流程与输入。

连续运行与转任务交接共用 Application 的唯一任务会话线程。`POST /api/v1/runs/start` 的 `repeat: true` 当前只开放蝎女；每轮仍调用同一 `prepare_task` 与 NativeRunCoordinator，保存独立结果。只有已静止、结果保存、三业务段完成、悬赏余量为零且无待确认报告/付款的 Completed 才续轮；异常、停止或转金币交接都不会继续刷蝎女。轮间十秒等待可取消，仍占据任务准入；`runs/current/stop` 同时取消续轮和当前执行。界面关闭不停止后台会话，服务重启不自动恢复会话。原 `run_scorpion_loop.ps1` 仅作为提交同一接口的命令入口，不再拥有循环、进程或停止文件。

`Application` 还拥有一份所有相关 Service 共享的匹配工作集准入。`recognition` 层按已知 ROI/模板/方法估算叶级临时矩阵，准入采用 RAII；每个 Service 拥有跨 worker 的单次装载像素缓存，时序游戏状态仍留在独立的 `Cache::assets`。`games/wvd/vision` 只负责具体匹配与候选优先级；`platform/windows/memory_diagnostics` 负责固定槽、预开日志句柄和 Windows 内存标量。工作集 256 MiB 与像素留存 128 MiB 都是工程调度目标，不是进程安全上限或历史故障证明。

输入默认只提交一次，页面结果由独立 AwaitResult 观察；业务明确声明的安全菜单输入可间隔重试，每次用新帧确认原菜单、目标未到和点击准入，保留首次结果期限及事件暂停。付款、报告、跳轮确认和送达未知的输入不适用菜单重试。识别 NoHit、Error 和外部阻断分开处理。收到停止请求先关闭新业务输入准入，随后取消等待并回收自有通道；未确认清理不能报告静止。初次启动可检查/开启已配置 VPN 并确保游戏在前台；普通续段不重复初始化。明确确认的 Pause 冻结、导航输入无进展、Auto-Move 冻结、战斗无进展，以及有已记录 `wait_7300` 意图的未知跳轮可交由任务层进行游戏级恢复；未知跳轮先完成可取消等待，所有恢复均须确认静止且不存在未决副作用。运行失败后同一协调器可观察绑定设备：实例明确退出才通过共用的可见启动入口重开，单纯ADB离线只重连，游戏进程已退出只启动游戏；随后按配置确认VPN并进入既有Boot流程，保持同一个BusinessRunState及原业务段，不重放已确认提交/付款。普通NoHit、黑帧、网络慢与Android尚在启动都不是重开实例的依据。

作者文档仍由唯一 WorkflowRepository 保存。运行前封存作者闭包、资源内容与版本，编译为 FlowProgram；历史 Maa 结果只读。正式 CMake 和打包只链接原生目标。旧 SDK 集成源码及过时验证工具位于 `next/archive/`，不是当前程序或测试的执行路径。

## 只读故障恢复

运行候选与待部署源码必须分开核对。ddc3366 收束仅做限定离线验收，见[收束记录](../../docs/reviews/closure-ddc3366-result-20260927.md)；已有实机记录仍见[观察恢复报告](../../docs/reviews/observation-recovery-20260927.md)。

- `devices/adb_failure.hpp`只分类单次命令结果；`ObservationUnavailable`仅用于明确的只读故障，NoHit、坏资源、OOM和输入送达未知不能借此变成重试。
- `devices/metadata_read_fault.hpp`仅把有完整静止、句柄释放、helper退出及零在途I/O证明的 `METADATA_TIMEOUT` 接回同一恢复链，携带本次预算、耗时和有界原报告；缺清理证明、取消或非法数据继续拒绝，不另设重试器。
- `FlowExecutor`拥有唯一的观察重试调度，保持原调用栈、任务目标和pending；连续故障窗口60秒，250毫秒退避至2秒。剩余窗口传递给底层读取，停止令牌取消在途调用；不会让ADB、设备层、Application叠加三套重试。
- 原Session工作线程通过设备能力端口核实绑定实例、创建标识和端口，调用既有生命周期动作；在线不重开，确认退出才可见启动。跨连接代次只用于原回执的结果核对，旧before不改，新的输入仍要求当前帧与精确代次。
- 业务有效观察预算扣除事件/只读故障暂停区间的并集；动画和纯等待按现实时间流逝，Run总墙钟不延长。一次成功截图不等于原输入前查询已恢复。
- `InputRetry`保留既有菜单默认语义，可声明`max_submissions`。用户最新消费边界是“購買”与同按钮绿色或紫色宝石组合禁止点击；普通资源不套用宝石禁令。住宿仅沿已选房型的原流程进入确认，不以通用OK开启任意购买；标准200G整句素材保留为现场证据，不因缺少其他价格/语言整句退役原住宿功能。住宿补点合计最多3次、5000毫秒间隔；业务状态保存实际提交次数和可能成本，住宿完成另由正面结果确认。非标准房价格未读取时成本记未知，不虚报200G。
- 所有pending均可只读核对；送达未知不得盲点，正面后置条件和旧控制资源静止须同时成立才能继续输入。报告、跳轮不因金币授权获得重放许可。
- `RunStore`分别保存最后有效帧与仅诊断的失败像素；后者没有FrameIdentity、不能用于识别授权。失败命令、期限、耗时、pending和清理结果分别进入现有journal/结果，前端显示原始读取原因。截图/日志保持有界，不因取证再次调用失败capture。

## 程序与观察所有权

- 循环仍由Application拥有：`repeat:true`且不提供`repeat_count`表示一直循环，提供正整数表示完成指定轮数后停止。计数只在原三段业务结算、结果落盘、零剩余报告及无未决付款全部通过后增加；停止或失败均不启动下一轮。工作台显示实际完成数/目标数，次数属于一次运行请求，不改用户游戏配置。

- 发布图为 `shared_ptr<const FlowProgram>`，同一请求的重复 NativeUnit 引用同一对象；RunDefinition 不可复制。每个 Session 的执行栈、命中计数、回执和事件状态独立，停止线程持有的 Session 覆盖程序/识别服务寿命。
- FlowProgram schema 7 显式保存 Definition 累计预算、检查策略/继承白名单、具名交接、纯业务 guard、Poll 与已知场景标记；Poll可附加业务进展条件，只有新的进展证据刷新节点无进展期限，场景仍存在不刷新。作者文档仍经正式发布重编译，历史图只读，不做旧 schema 自动回退。预算来自原声明，新 Call 签发新期限，内部跳转不刷新；嵌套事件暂停父有效时间，Run 墙钟始终继续。
- 安全菜单重复是Input的显式可选策略，不是第二套输入/恢复框架。原生工厂用`retry_menu_input`，作者识图click用`menu_retry_interval_ms`（1–60秒）；每次走相同新帧准入和回执，不清父pending，不刷新首次结果期限。网络处理器只能重试自身已确认弹窗按钮，不能重放父业务输入。右下小ROI运动仅暂缓重试，不证明网络完成或游戏死机。
- 明确可重复输入多次尝试耗尽本次结果期限时，只有送达已知且新帧仍确认原页与重试条件，才能放弃该次未生效尝试并进入已声明的业务失败分支。此操作不宣告成功；付款、送达未知与子事件的父pending不适用。导航/Auto无进展可由任务恢复策略进行游戏级恢复，缺素材和未确认副作用不得升级为重启。
- 计数按事实归属分离：非输入步骤没有默认全程经过次数；原生输入限制连续未确认尝试，仅AwaitResult的新帧正向结果才结束该次计数。作者显式repeat_limit仍约束其声明的业务循环。正常场景结束连续未知诊断窗口；网络每次进入独立事件帧，恢复后再遇网络提示不累计前次失败。所有路径仍受声明的节点/调用/Session期限和停止门禁约束，未确认付款等副作用不得重发。
- FlowExecutor 的 ObservationCycle 只保留本轮帧和同有效事件作用域的覆盖层 NoHit；输入、等待轮询、事件返回、身份/TTL失效后重新取帧。技能索引通过游戏层只读业务谓词选路；不是视觉命中，不授权点击，也不消费技能次数。场景和目标仍走 InputGate 单次消费。
- 结果先保存无分配安全事实并回收输入，再构造富诊断。`details_complete=false` 在 API/历史/终态明示，不能清除未决输入、报告 Completed 或自动恢复。WORKER_ABORT 是最外层兜底，不替代正常存储错误。
- 最近帧 JPEG 由 RunStore 的受控单线程与单 pending 槽完成，另最多一张 in-flight，结束关闭接收并 join；不移动业务/故障证据到可丢弃槽。匹配日志 OS 标量采样/普通写入至多每秒一次，资源失败仍即时记录。
- EventJournal的UI环保持有界，执行事件通过RunStore单一回调先写`execution-events.jsonl`，不随环淘汰丢失早期角色选择及恢复记录；终态只由原子提交的result声明。内存收尾分为Session释放、定义释放、线程join及批次配置释放，后两类诊断独立保存，不能改写已经冻结的终态日志计数。详情与配额见[data-authority.md](data-authority.md)。
- 性能复用固定数组分类和次数；主线程嵌套耗时排除子段，并行 worker 耗时不与墙钟相加。Session在节点/事件切换或终止时记录连续执行段，普通轮询不逐帧写盘；NativeFlowPorts记录输入提交耗时，FlowExecutor只在实际结果命中时记录confirmed，未结清则记录unconfirmed。由同一RunStore保存每轮`action-timing.jsonl`，不依赖256条事件缓存、不新增性能后台服务。64MiB/轮上限、写入失败和落盘耗时进入diagnostics.action_timing；墙钟、worker和包含关系不可重复相加。旧候选未热更新时仍只有原汇总，具体口径见`docs/reviews/action-timing-20260927.md`。
- 正常战斗/开箱按各自 Definition 推进；内置异常和特殊处理器只在当前结果持续不符时按优先级分派，不再给每个正常节点包裹整组 `!blocking_screen`。动画/加载去抖为 1 秒，仅控制诊断开始与限频，不是页面跳转期限；处理器恢复后核对原回执，不重放输入。作者显式 overlay 抢占规则保留。详见 [分组与恢复边界](../../docs/reviews/flow-check-dispatch-20260926.md)。

## 本轮修复后的责任边界

- `authoring/workflow_validator` 只校验作者文档结构；`storage/WorkflowRepository` 保存正文及独立的内置来源元数据，显式 CAS 同步前保留旧文档备份。固定 `expected_revision` 的调用不被自动改写。`games/wvd/tasks/run_builder` 决定任务工厂、轮数、7300 秒等待及恢复条件；Application 冻结配置与定义、编译预检后才连接设备。
- `workflow/FlowProgram` 的 Call 将子定义的业务失败作为有类型的返回，不把有效子任务 ID 或框架完成态当作业务成功。`runtime/FlowExecutor` 由唯一会话线程推进调用栈及事件栈，普通失败走调用者的失败边，致命错误终止会话；`BusinessConfirm` 只在观察到正面业务证据后改变 WVD 计数。`RegisteredOperation` 通过门禁发一次输入，结果不明时保存未确认回执并禁止重发。停止先封输入，再取消等待/回收自有设备通道，未静止不能标为完成。
- `FlowExecutor::progress_snapshot()` 只给出当前步骤/调用栈/事件与未决操作标量，Coordinator 存入历史并同源展示在工作台。HTTP 不推进流程；事件与输入的所有者仍是会话线程。
- 成功 `Return` 只校验即将弹出的子帧 pending；父输入的回执继续挂起，到事件返回后的新帧确认。根终点和业务失败仍核对全栈未确认输入。有序启动/对话候选首命中后不安排无关尾项，`all/any` 与必须全量的反证仍传播已执行的全部 Error；资源 OOM 优先于同批次 Hit。
- `resources/authoring/semantic-assets.json` 是人工配方源，打包同步到资源包并校验清单；作者及旧原生映射在编译/发布前解析。模板和 OCR 都保留完整条件字段，诊断与正式运行共用同一识别 Service。城市公共配方、繁中公会页及开箱/选人/奖励动态探针由同源目录生成于构建期，发布身份带源哈希且打包拒绝 EXE/资源包错配；其他内部动态 boot/地点探针尚未全部做到发布期依赖收集和语义冻结，不能宣称 R05 全面闭合。

### 素材识别方式

- `variant.condition.mode` 是素材默认算法，`variant.alternatives` 是该语言下明确配置的替代配方；流程使用 `semantic(id, method?)`。未指定 `method` 跟随默认，指定后只执行对应配方，不自动 fallback、换语言或降阈值。不另设重复的 `type` 权威。
- `SemanticAssets` 在编译期把选择展开为完整条件；`WvdVision` 负责 mode 分派，`recognition::Service` 负责帧身份、ROI、模型、同帧缓存和统一 Hit/NoHit/Error。业务流程不保存另一套算法选择器。
- OCR 字段是 `language/expected/match/threshold/unique/roi`。已配置英文和繁中模型；定位用 OCR 必须 `match=exact, unique=true`。多个合格文字框为 NoHit、无坐标，模型错误为 Error；不以流程终态反推识别成功。坐标是全文文字框中心，经现有点击门禁消费，不发固定坐标输入旁路。
- 模型来源、SHA256、包内目录唯一记录于 `resources/recognition/ocr-models.json`。准备阶段下载并校验，构建编入锁，打包校验构建锁并携带全部模型，发布检查所需模型，运行懒加载选定语言。不在游戏运行期联网补模型。
- 每个识别 Service 最多各保留一个英文/繁中引擎；同帧最多缓存4个ROI的原始文字框，每项最多128框、64KiB，不持有历史像素。换帧/动作代次即清空；当前缓存计入已有资源统计。模型由会话释放，OCR大模型不因此被宣称“内存问题已解决”。
- 要塞入口与8个可见区域提供繁中 OCR 选项，默认仍是模板；编辑器读取真实目录按语言展示可选方式。详见 `docs/reviews/recognition-methods-closure-20261002.md`（仓库根目录）。
- 公会悬赏 Reveal 由原生任务调用同一 `PublicFlowLibrary` 闭包。已处于目标页时零输入；打开列表不是跳轮或提交。英文 Report 保留旧源码的准备、提交后置确认及计数契约；繁中缺 `guild.report.action` 配方的完整任务在准备期拒绝，不进入跳轮或讨伐后才失败，也不拿奖励按钮猜提交点。

## 工作台与存储准入

- Vue 的 `useRunSession` 由 App 创建一次，持有唯一运行/设备镜像及未确认启动身份；每类 GET 最多一个在途，1500ms轮询。读取失败或超过5秒未成功更新即显示未知，保留最后事实，禁止新启动但不禁止停止尝试。两个编辑页不拥有轮询或业务调度。
- 配置和流程草稿分别持有写锁及操作代次，异步写期间冻结快照并禁用编辑与导航，停止按钮在锁外。任务有效配置读取不是持久化基线，只有最新选择可替换草稿；CAS冲突不自动吞掉草稿。
- Application 在新请求幂等查询后，以及 `prepare_task/prepare_workflow` 前检查数据盘 available 至少1GiB；查询失败和空间不足分别拒绝新准入，不删除历史、不自动恢复循环。计时不完整、诊断失败或终态保存失败沿既有结果/secondary_errors阻止续轮及交接。
- 迁移盘点只保留冻结审计工具与归档证据，不再参与产品导航、加载、常规构建和发布。正式资源仍来自原pack与作者资源目录，未增加替代资源权威。

## 尚需核对的边界

本轮公共步骤、作用域策略和迁移边界见 [流程分类工作包](../../docs/reviews/flow-classification-repair-20260926.md)。八个细粒度作者步骤与原生任务共用同一 WorkflowRepository 冻结闭包；编译期 PublicStepScope 不持有运行状态。Definition 策略决定继承规则，节点名/目录名不决定权限。Call 的具名交接只解释导出端口，外部未绑定时拒绝发布；只有真正的业务确认可以改变路线点、策略消耗和报告计数。

- 原生任务生成器当前使用项目中立的发布期 JSON 图描述，经 `native_program.cpp` 严格转换为强类型 FlowProgram；运行时不解释该 JSON。工厂直接构造强类型步骤尚待完成。
- 离线正式入口已证明作者流程保存重开、编译执行及一个原生任务进入其步骤；不能证明真实 MuMu 输入、Clash、繁中页面或完整蝎女任务可用。
- 王城身份必须由塔楼背景确认，城市图标只作动作锚点。悬赏跳轮刷新后不领取，讨伐后逐项提交；普通委托才有接案。哈肯“歸還”先返回郊外列表，再回城。
- 真实维护页面素材、常态白色道具店、新设备后端长期取消和资源稳定性仍缺对应证据。旧 Maa 的资源未归因结论只属于历史路径。
