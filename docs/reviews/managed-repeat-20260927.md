# 连续循环控制接入

## 用户可操作边界

此前单轮由正式后台执行，循环却由外部脚本串联，界面无法完整管理。这轮将循环接回同一 Application，不要求用户管理后台进程或停止文件。

- 工作台蝎女任务提供“连续循环”复选框；开始请求带 `repeat:true`。其它任务不冒充已具备完整循环结算条件。
- `runs/current` 返回 repeat.active/state/completed_cycles/reason。轮间仍显示忙碌，禁止另一个任务或设备操作抢入。
- 停止调用 `runs/current/stop`，不绑定可能过期的上一轮ID，同时取消下一轮与当前执行。取消在下一轮提交锁内再次检查。
- 同一个任务会话线程处理循环或旧转金币交接，不同时运行两套守护逻辑。交接完整保留，但不能冒充蝎女结算成功后继续刷蝎女。
- 保留原生单轮结果、历史帧和异常诊断。新一轮只在前轮落盘且静止、3段完成、报告余量0、无待确认提交/付款、无拒绝/次级错误后开始。配置冻结于本次启动，修改后停止重开生效。
- 浏览器关闭不影响已授权循环，重新打开可查看/停止；服务退出取消循环，服务重开不会自行继续。
- 原PowerShell入口改为一次性提交同一API，不再拥有循环；旧STOP未删除，作为历史调查产物保留，不参与新会话状态。

## 验证

只构建受影响 automationd 与 Vue，并整理 candidate23。未跑旧矩阵或资源长测。

候选SHA256：`b0a424e710fff2fb2b86b66b4d3e422ce8d2d9aaa0bdcae30bb49604e7f833c1`。

实例 `A969ABB0-03E7-4110-8315-1442BFB31648`，正式界面启动请求 `5013bc4b-6ade-439a-8e4d-b99511f33368`。根run 1已Completed：213.74秒，3业务段、提交1个报告、住宿1次、56输入/0拒绝，结果已落盘且静止。本轮自然经过 `ListBack@await -> CloseReveal@await -> GuildLeave@await`，现场已有“已記錄懸賞令”的公会菜单，补齐了迟到展示卡的实际交接证据。采集目录 `next/.local/c11-flow-product/bounty-cycle21-050147/`（沿用观察脚本历史命名前缀）。

后台自动续起run 2，接口返回 `repeat.completed_cycles=1/active=true`，界面显示同样状态。通过界面的停止按钮（键盘激活）停止run 2，落盘 `UserStopped`、`quiescent=true`、`repeat.active=false/state=stopped`，之后没有自动续起。浏览器自动化鼠标点击未立即观察到请求，不能凭点击工具返回声称已停止；以上结论以真实终态和输入静止为准。

停止验证落在7级技能确认页。随后人工按已观察画面确认该技能、快捷回哈肯、归还、回到城镇，仅作恢复启动位置，不计作自动任务通过。通过同一界面重新启动请求 `ea912b39-ce56-4478-abda-13032fbb3487`，根run 3开始循环，正式链自动收取第二轮遗留报告。该会话随后完成31轮，并在run 34因模拟器崩溃停止，详见下节；没有外部PowerShell循环进程，服务PID为34088、端口17654。

此轮已证明一轮结算、自动续轮、用户界面停止和重新启动；长时间稳定性仍待运行记录，不由本轮证明所有随机异常。服务会话计数在重新开始后从零计，历史各轮结果仍保留。

## 07:12 模拟器崩溃调查

2026-09-27 11:06只读核查：后台PID34088及API仍正常响应，MuMu管理服务仍在，但设备实例进程已退出，ADB设备列表为空。没有执行重启、游戏输入或源码修改。

- 实例2的`logs/shell.log`第13518至13522行明确记录07:12:41的`Graphics Driver crash`、错误码901、`crashModule=nvoglv64.dll`、`isGraphicsCrash=1`及实例2退出通知。原始日志位于本机MuMu安装目录下的`vms/MuMuPlayer-15.0-2/logs/`，不是项目硬编码路径。
- 会话run 3至33已完成31轮；run 33为Completed，报告1、住宿1、218.3568秒、结果已落盘且输入静止。
- run 34于07:11:48开始，最后位于`Task_Leaped`，frame94记录`bounty_leap_completed`，即跳轮已完成，尚未进入本轮战斗。随后报`ADB_TRANSPORT_FAILED:error: device offline`。
- 输入释放报`INPUT_RELEASE_UNCONFIRMED`，根任务终态为`Interrupted / NATIVE_INPUT_CLEANUP_PENDING`。循环没有继续启动下一轮，但API仍显示`busy=true/quiescent=false`，`handoff=blocked/SOURCE_CLEANUP_PENDING`。`repeat=stopped`却未携带原因是另外的状态展示缺口，尚未修复，不是后台进程闪退。
- 证据：数据目录`runs/A969ABB0-03E7-4110-8315-1442BFB31648/34/result.json`及`recent-frames/20260926T231238460_r34_f94.jpg`；历史帧文件名使用UTC，实际本地时间为07:12:38。

结论仅限于已确认的MuMu图形驱动模块崩溃和设备离线传播链；尚不能归因到驱动版本、模拟器缺陷、GPU资源压力或IPC调用，也没有证据将其定性为OpenCV内存泄漏。

## 崩溃恢复缺口修复

用户要求对照旧源码补齐恢复，不再沿用“所有故障一律停下”的阶段限制。

旧`src/script.py`的`DeviceShell -> ResetDevice -> CheckAndRecoverDevice`会恢复ADB并启动缺失实例；`restartGame`按游戏、ADB、模拟器逐级恢复，`EnsureClashVpn`与`WaitGameBootReady`分别负责VPN和进入游戏。这些行为原生版没有完整承接：

1. `lifecycle_execution.cpp`将RestartInstance前置条件固定返回false。
2. `run_builder.cpp`只给已命名的游戏卡死生成应用重启计划，设备退出和ADB断线没有对应计划；协调器还只准入游戏级操作序列。
3. `scrcpy_control.cpp`把静态远端jar未删除也当成输入未释放。原现场实际为`child=0,forward=1,uploaded=1,socket=0,held=0,keys=0,unconfirmed=0,unresolved=0`，阻塞了恢复与任务准入。
4. MuMu崩溃状态带有效实例身份但error_code=900，初始绑定只接受0。实机还证实：重新启动时进程已起来、Android尚未就绪，旧900可能短暂保留；必须等待引导，不能误报实例身份变化或反复启动。
5. 循环守护遇清理未完成直接return，未将失败原因写进repeat状态。

修复仍使用唯一NativeRunCoordinator/DeviceSession/LifecyclePlan：失败后观察绑定实例，明确退出才重开，实例在而ADB断线只重连，应用进程已退出只启动应用；按冻结配置处理VPN，再经已有Boot识别。共用配置的MuMuNxDevice可见窗口入口，不使用管理器restart去创建后台实例，也不杀全局ADB或其它模拟器。

静态jar删除失败保留诊断，不再阻塞输入静止；输入通道、子进程和自有转发仍须清理。实例已退出时允许撤销不再存在的按键持有状态，但不能抹掉业务层未决输入。恢复沿原业务段、保留BusinessRunState；未确认提交、付款或跳轮不得盲目重放。界面明确显示清理失败原因。

只增量构建automationd和已有test_native_devices；后者增加崩溃元数据/启动过渡态、无退出证据不得重开、取消不得启动、确认退出后的生命周期顺序检查。没有运行旧矩阵，假端口检查不计作真实MuMu恢复通过。

实机记录：candidate24通过正式入口打开原已崩溃实例，但在保留旧900的引导过渡态误报MUMU_INSTANCE_MISMATCH，未启动任务；修复后candidate25经同一正式入口连接已引导完成的实例并启动游戏。它在繁中下载页首次提交前遇到INPUT_CONTEXT_CHANGED，底层未发送下载点击，但执行器将拒绝直接转为整链Failed。这不是再次闪退，模拟器和游戏仍正常。

candidate26修正这一启动交接：只有底层发送前的INPUT_CONTEXT_CHANGED返回原选择点重新观察，或在已有菜单重试中保留pending等待；原期限不刷新，其它拒绝和送达未知仍不允许重发。设备同时记录前后焦点、视口或查询失败原因并使元数据缓存失效。循环保留拒绝计数，但以真实Completed与业务/清理回执决定续轮，不因已经安全恢复的旧帧拒绝再停成功轮；真正失败则直接显示原始失败原因。源码编译通过后正式入口重新启动，最终现场结果见项目当前事实。这不是重新制造一次显卡崩溃，也不能冒充运行中断点自动恢复已完成实测。

candidate26实际通过Boot进入王城、公会和悬赏列表，但开局复查旧报告期间晚到的“新的悬赏令”卡片不在该阶段分支中，停留于RecheckOldReports。通过正式停止接口得到UserStopped/quiescent=true，未手动关闭弹窗。candidate27把已有关闭素材接入InspectBoard、CheckOldReports和RecheckOldReports，关闭后回悬赏页重新判断，不能把展示卡代替刷新完成或“无报告”的证据；Boot只以卡片标识与关闭按钮的组合确认已进入游戏，后续处理归任务所有。沿用已有素材，没有重裁图或增加另一套流程所有者。

candidate27在提交预检时暴露resource缓存未注册新引用（COMPILE_IMAGE_SCAN:Accept:invalid map<K, T> key），因此没有启动任务或发送点击。candidate28补齐`location_probes.hpp`中与CMake资源清单对应的缓存项，再从同一展示卡现场执行；此失败保留，不记作成功验收。

candidate28的Boot组合仍未命中：新增的WANTED列表卡头ROI不适用于全屏展示卡，不能混用这两种布局。正式识别接口对现场截图的既有底部关闭锚点返回Hit；candidate29撤销多余的卡头限制，Boot仅确认已在游戏内的详情页，不据此判定城市、刷新或业务完成。沿用素材阈值，任务层仍自行关闭展示并检查真实悬赏列表。

最终部署candidate29，EXE SHA256=`f8f66967aecfd6db9891e52c4687ba13a1969309798119d926655532ee09dde8`，PID11032，端口17654，数据目录不变。请求`b597cbee-bcb9-4946-903c-7a51f82abe9a`、实例`76D3FFDB-E484-4ACE-91F1-F4E6390E4C7E`的run 1已从遗留展示卡自动经过复查列表、退出公会、跳轮和入本，最终核对节点为`Task_FirstDungeon_Route0_Select0@await`，Running/repeat.active=true，保留循环。没有手动点击卡片，没有再次重启模拟器；完整一轮和运行中再次实例崩溃后的自动续段不能由此次局部恢复外推。

原始日志已复制到`next/.local/c11-flow-product/crash-20260927-071241/`（mumu-shell.log、mumu-vbox.log），避免模拟器重开轮转原日志。Windows System对应时间段未查到Display/nvlddmkm/WHEA事件；主机GPU为RTX 3080 Ti，驱动版本32.0.15.9186，仅记录事实，未擅自调整驱动或图形设置。当前候选与最终实机结果见项目当前事实。

## 13:44 郊外停机与容错审查

### 现场和证据边界

candidate29的连续会话累计完成28轮，第29轮13:44:12落盘Failed。run28为3段Completed；run29第一段Completed，第二段flow_code为ADB_TIMEOUT、unresolved_input=true，根reason被汇总为NATIVE_INPUT_RESULT_UNCONFIRMED。服务仍响应、busy=false/quiescent=true；绑定实例2/PID33512仍运行，ADB 127.0.0.1:16448在线，不是已证实的再次模拟器闪退。

用户指出当前停在郊外后，通过正式capture接口取新帧并等待completed，再保存`next/.local/logs/run29-current.png`，确认是郊外迷宫选择页。这不是13:44时刻的录像，不能倒推所有转场细节。上一轮把13:44:04的历史王城帧当停止现场不严谨，已纠正。

同一当前帧经正式识别接口、zh-Hant、生产默认0.8阈值识别`outskirts_abyss_zh_hant`得到Hit，box=[557,762,198,59]，说明当前“初始的奈落”可以用已有素材识别，不需要重裁图或降阈值。未发送游戏输入、未恢复循环、未构建、未修改运行配置。

证据来源：实例`76D3FFDB-E484-4ACE-91F1-F4E6390E4C7E`的run29/result.json，事件271至284。按最后一次输入尝试相对计时：

| 时刻 | 事实 |
| --- | --- |
| -536ms | Step0Fallback2Present尝试点[788,831]，对应王城郊外入口 |
| -255ms | 该输入accepted |
| -12ms | input.observed，流程认为其后置条件满足 |
| 0ms | Step0Fallback1尝试点[1,1] |
| +292ms | 输入accepted并进入await，delivery_unknown=false |
| +514ms | 记录waiting_for_business_result |
| 后续 | session.ended记录ADB_TIMEOUT，未生成任何recovery事件 |

这不是普通AWAIT_RESULT_TIMEOUT，也不是已经证明点击没有送达。当前郊外画面与入口点击相符；不能拿稀疏历史帧认定没有进入郊外。

### 已确认的代码缺口

1. **观察失败直接杀死流程。** `DeviceSession::capture_impl`即使通过MuMu IPC取得像素，每隔约1秒仍会查询前台/旋转元数据。`foreground()`的`dumpsys window`为5000ms命令期限，异常直接抛出；`dumpsys input`则catch后置rotation=-1。`FlowExecutor::tick`将抛出的std::exception直接转为fail，`NativeExecutionSession::run`随即退出并清理，而不是保留当前输入等待、暂缓输入后重试只读观察。这条共享路径同样影响战斗、宝箱和其它导航，不只蝎女。
2. **pending把恢复入口挡住。** `native_run_coordinator.cpp`先把unresolved_input映射为根失败原因，再在设备观察之前break。普通[1,1]点击、菜单点击和付款/提交没有在这个入口分开。不能通过清空pending或重放所有输入修复；应保留回执，在原等待点恢复只读观察，先确认实际页面。已发送结果未知不等于禁止只读检查设备/页面。
3. **短暂超时后设备仍在线没有对应恢复路径。** 即便去掉pending阻挡，现有恢复策略主要接收实例退出、离线、应用退出和特定卡死理由；ADB一次查询超时、随后设备正常在线并不在其中。仅补“崩溃重开”不能修复这一类现场。
4. **入本后置条件过宽。** `navigation/dungeon_entry.cpp`以所有路线锚点集合known构造post=known或inside或worldmapflag。known含原城市，所以“点郊外后还是王城”也可能满足post，日志确实在accepted约243ms后便确认，再进入[1,1]。这是确认语义缺陷，但不能声称它制造了本次ADB超时。
5. **入本未完整承接同页菜单重试。** `enter_dungeon`没有调用已有retry_menu_input。目标点击确认后直接进入下一个数组步骤；如果点击实际未推进，宽post却通过，下个楼层步骤只能运行其fallback，[1,1]并不能重新选择上一级初始奈落。应按当前真实页面重新分派城市入口、迷宫名、楼层、进入按钮，不能让内部步骤号替代页面事实。
6. **异常诊断缺失。** `AdbCommandClient::require_transport`只抛ADB_TIMEOUT，没有命令用途、超时值、实际耗时和输出摘要。run29的diagnostics.failure_attempts=0、entries=[]；当前native生产源码中save_diagnostic只有声明和定义，没有调用。已有recent-frame是每15秒采样，不是失败即时截图。像素可能已经拿到，但元数据查询失败会让capture_sink也收不到这一帧。

### 尚不能定论

第二段计时中capture_metadata约5.574秒、pixel_capture约0.713秒，设备当前fast_capture_disabled=false、connection_generation=1；结合异常传播位置，5000ms的前台查询是主要嫌疑。**历史日志没有具体命令记录，不能把嫌疑写成确认的dumpsys window超时，也不能确定Android调度、ADB客户端或模拟器内部为何迟延。** 没有本次显卡崩溃或游戏网络错误框的证据。

### 旧版对照与修复边界

旧`src/script.py:DeviceShell`在RuntimeError/TimeoutError后恢复ADB/设备并continue；`ScreenShot`在截图故障后重试；`FindCoordsOrElseExecuteFallbackAndWait`反复观察目标、处理阻塞、按间隔执行fallback。旧实现不完美，也不应照搬其全局重启ADB或盲目重放命令，但它在临时读取失败后继续观察的能力没有被新链等价承接。

下一步应在原架构内完成以下修复，不能只扩大秒数或去掉所有保护：

- 对取帧/只读元数据查询的可恢复传输错误，在原会话保留调用栈、pending与业务账目，暂停游戏输入，等待并重取；连续故障受停止可响应的时间窗口约束，正常观察恢复后清除本次连续故障状态，不按整轮累计偶发次数退出。
- 只读重试和必要的绑定设备重连分层处理，未确认实例退出不重启模拟器；不把通用shell_fixed整体变成自动重放器，避免再次执行付款或其它有副作用命令。重连后的帧身份也须重新核对，不能复用旧观察授权输入。
- 入本按明确页面及预期下一页面确认，原页仍存在时仅对已声明可重复的菜单动作重新识别并重试；明确进入下级页就跳过过时fallback。不以固定延时、任意已知锚点或[1,1]充当进展。
- 错误保留原始ADB原因与上层pending原因；记录命令用途、期限、耗时和恢复阶段，终态保存最后有效帧及其时间/新鲜度。元数据失效的像素只能作诊断，不能授权输入。

本节为只读调查结论与修复清单，不代表这些缺口已经修复。没有为复现而断网、杀模拟器或重复运行游戏。
