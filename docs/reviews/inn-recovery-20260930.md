# 住宿恢复与完整链路复核

## 问题与修复

09:38停止不是游戏闪退。candidate33第1轮已经战斗并提交1份悬赏，住宿付款提交0次，停在标准房200G确认页。`Task_Inn_SelectConfirm`遍历`PendingPaid`条件时，重复嵌入城市反证，导致`default_dialogue`内部模板递归超过8层，抛出`WVD_CONDITION_DEPTH`。设备观察当时明确连接正常、游戏运行、实例未退出。

修复范围为正式`rest_at_inn()`工厂：

- 等价展开城市反证，提取付款结果页组合，删除被确认按钮存在性涵盖的重复后置反证，不提高递归阈值、不吞识别错误。
- 已准备但尚未提交付款时，允许重新识别房型页、住宿页、城市入口并继续，不清除原消费账目。
- 可从明确的繁中标准房200G确认句恢复。不是任意“确定”按钮都能授权付款。
- 已提交付款保留原回执核对与有限补试；送达未知不盲目重放，宝石购买保护保留。

## 有限核对

- `test_native_author --inn-frame <实际失败PNG>`：住宿62个正式观察、后置、重试条件无Error；确认页Hit，付款回执/城市/宝石购买NoHit。即使标记已发送，仍不能把原确认页算作住宿成功。
- `--inn-transitions`：保留原有付款后加载恢复、未付款城市不算成功两项；补充未付款退回房型页后重新选房、仅提交一次并退出。使用正式业务图、执行器、账目，视觉叶子为受控替身，不冒充实机通过。
- 只构建受影响的`automationd`与`test_native_author`，未执行旧全任务矩阵或资源长测。

## 实跑发现的启动期限问题

candidate34首轮尚未进入业务便在131.01秒停止：启动中自然发生网络错误，重试后资源下载到100%，但`wvd.boot`累计120秒将仍在推进的启动过程截断。原失败记录保留，不能算完整一轮通过。

修复将启动/通用页面分派的120秒约束限定为未知页面无进展窗口；已识别的输入使用原输入后置期限，网络等子流程使用自己的期限。确认成功后重新进入分派；普通轮询不刷新时钟。调用方任务总期限仍在，不再给启动额外签发累计120秒。计时包装保留原识别条件，并在按钮自然消失时允许重新选路，不强点旧位置、不重放付款等动作。

`--boot-progress`只核对该正式图和执行器的三种受控时序：已识别动作等待超过未知窗口仍可完成、点击前页面变化重新选路且无输入、真正未知页面仍按无进展窗口超时。未用这些受控结果替代实机。

## 候选与实机

- candidate34（仅住宿修复，启动失败证据）：`de8ec2cc1428ee370a425f8da5c2e01491f2066979ebdf86d31064350fd01600`。
- candidate35（住宿和启动修复）：`63e7c587aacf74820da58f5d444c0a2c0e96148cacf9c3de8381921391bafde0`。
- 原candidate33、正式配置、用户流程及原失败日志保留。除程序和身份说明外，595个部署文件逐一哈希一致。
- 实机证据根：`next/.local/inn-fix-930`；candidate34服务实例`E453B0BC-E7F7-4508-A5F4-6574AA5AB585`的run1为上述启动失败。
- candidate35服务实例`302CAF2E-33F8-4ACF-87AE-60C58B74A16C`，run1完整一轮请求`c48c102a-445d-4bcf-9fa5-bc8796287db0`已Completed，耗时285.4370534秒。三段业务完成，59次输入接受、0拒绝，静止、结果落盘、诊断完整性均确认。
- 实际路线包含：网络弹窗恢复进入王城、悬赏页检查、跳轮/刷新、奈落B2F导航、战斗1次、返城、悬赏报告提交1份且余量0、标准房200G提交1次、住宿完成并退出到王城。结束后通过正式取图接口重新抓取`final-one-cycle-city.png`核对王城，不沿用历史帧。
- 新100轮请求`30ce5fc8-b977-448e-bf01-dd5ffd91f138`已由同一Application接收；12:52:59确认run2处于Running、repeat.active=true、target_cycles=100、completed_cycles=0。验收那一轮不计入新100轮，也不拼接历史42轮。未宣称100轮已经完成。
- 自然出现的繁中网络弹窗已由正式启动流程重试并继续；不制造断线，不额外修改VPN。

## 证据与边界

- `final-one-cycle-summary.json`保存最小完整回执；正式run1的`result.json`、`action-timing.jsonl`保留动作/业务时间线。`loop100-request.json`、`loop100-accepted.json`、`loop100-running.json`分别记录请求、接收与实际运行，不混为完成结果。
- 正式`profile.json`与部署前副本哈希相同。原用户流程、config、mod和旧日志没有清理。代码本轮未commit/push。
- 100轮由既有Application循环所有者运行，可在17654工作台停止。滚动历史帧240张/128MiB/15秒限频；异常PNG、每轮结果和动作耗时单独保存。失败留证，不自动篡改结果继续凑轮数。
- 本轮自然网络弹窗、标准房付款及整条蝎女正常链有实机证据。未付款退回菜单、付款补试、持续未知页等仍仅有对应受控核对；未制造故障。单轮通过不证明长期稳定、其它任务或所有随机分支通过，资源未归因边界保留。

## 后续双按钮网络弹窗

100轮会话实际完成19轮（candidate35的run2–20），run21进入荒屋时失败，原因为`FLOW_INVOCATION_TIMEOUT:Task_TimeLeap_Entry`，208.35秒。现场繁中正文仍为“發生錯誤，請確認網路環境後再試一次”，但按钮变为左侧“返回標題畫面”、右侧“重試”。

旧`src/script.py`的`TryPressRetry()`先找下载/重试，再由`TryHandleCommonBlockingScreen()`考虑返回标题。新版已承接优先重试的处理顺序，却把繁中按钮ROI固定为`[260,800,380,220]`，右界640裁掉右侧文字。原模板84×39，旧ROI最高分0.5852，完整按钮行`[100,800,700,220]`为0.9819，故根因不是繁中正文缺素材或阈值太高。

修复同步原生`network_probes.hpp`、作者语义源及生成包；不降低0.86阈值，仍要求网络正文和重试文字共同命中，输入点取模板中心，不能照搬旧固定`[450,900]`点击双按钮页。公共流程生成副本的下载条件同步为已有作者源，与candidate35已部署版本一致，不引入额外运行行为。

现场证据根为`next/.local/network-fix-930`。run21账目虽标记跳轮准备，但逐项输入日志显示只打开了荒屋及两次单按钮重试，没有提交跳轮、报告或住宿；原失败记录不清除、不改写。后续从现场重试并重新收敛页面，不假称恢复了已结束的原会话。

`--network-layouts`使用正式识别Service及真实PNG核对通过：单按钮中心`450,898`、双按钮中心`635,898`，住宿确认负例NoHit；同时核对作者配方与原生配方相等。未改素材、未降阈值、未跑旧矩阵。

candidate36已构建：EXE `7183c7f22a6fe364be5ca55ddb61f4004c6973c5c27e2218c13566deaa090407`，资源包`baf41851d15a4db35342fbb2cf0c0b52a8474e9d3b0a4b21d4280eb898e978fd`。相对candidate35仅EXE、构建身份、资源manifest及语义目录哈希变化，其余部署文件保持一致。`git diff --check`通过。

首次部署尝试被工具策略拦截，旧服务已正常释放设备连接，当时并未宣称新候选生效。用户再次明确要求继续部署与循环后，重新核对Failed、静止、结果落盘、循环关闭、设备断开及进程路径，成功结束旧PID46092。candidate36已在原17654部署，PID46896；游戏、模拟器和VPN没有关闭，配置哈希与部署前一致。代码本轮未commit/push。

实机恢复通过：新服务实例`BF08E67F-3BB8-4D08-AC69-A610CDA59443`的run1由正式启动流程命中双按钮网络弹窗，点击右侧`(635,898)`，186956000ns后确认弹窗消失。随后正式任务退出荒屋、回城检查悬赏页并继续跳轮，没有人工代点或返回标题。

剩余81轮请求`d39d875e-e4e8-448f-beed-6ca0bb199452`曾接收并运行。15:19:39确认Running、target_cycles=81、completed_cycles=0、repeat.active=true；旧19轮与新会话分开记账。请求、接收与当时运行证据分别为`resume81-request.json`、`resume81-accepted.json`、`resume81-running.json`，不代表当前仍运行。

## 15:38进程消失复核

17:53只读复核发现17654无监听，automationd、wvd-capture-host及MuMuNxDevice不在，MuMu管理器及其服务仍在。candidate36正式run1–4均有Completed、quiescent=true结果，各有战斗1次、报告1份、完成悬赏循环1次；分别耗时284.55、284.01、244.76、274.43秒。加此前19轮，共确认23/100，剩余77轮未完成。

run5没有result.json，动作日志最后写入15:38:27.781。15:38:06点击荒屋准备跳轮，随后自然网络弹窗；15:38:24.435重试输入confirmed，15:38:27.781依次记录NetworkOverlay_Cleared及NetworkOverlay_Terminal。最后保留帧`20260930T073819591_r5_f51.jpg`是“連接中”加载画面，不是当前现场，也早于重试完成。后台stderr为空，stdout只有READY；未发现本时段Application Error/Windows Error Reporting的崩溃记录。最后内存采样private=270532608字节，约258MiB，不能据此归因于内存溢出。

同一时间Windows System日志显示15:38:28启动安装`9PLM9XGG6VKS-OpenAI.Codex`，15:38:30安装成功；Application事件44665/44666/44668的原始EventData分别为沙箱服务收到停止请求、服务停止、服务运行，对应15:38:29.751/29.752/30.641。它们是CodexSandboxService提供者的信息事件，不是同编号的应用崩溃事件。

更新与服务重启紧邻运行日志中断，外部生命周期中断是优先调查方向；没有进程终止记录或退出码，尚不能确证更新直接结束了这些进程。后台自身不在时，进程内游戏/模拟器恢复逻辑不能执行。本次未改代码、未重启工具或游戏、未恢复循环，保留现有证据。

## 安卓VPN与网络重试修复

18:08重新启动candidate36后，首次启动run1在81.38秒失败。准确原因是Boot_NetworkZhHant的后继HandleNetworkObservationEnd已正常经过一次，却被max_hit=1禁止第二次经过；ObservationResume也有相同问题。后续重新提交的77轮请求实际Completed35轮（run2–36），run37在21:06:35以INPUT_RESULT_UNCONFIRMED:AWAIT_RESULT_TIMEOUT中断。网络输入日志只有1次底层提交，后续帧423/424等仍为menu_retry_not_ready、attempts=1。右下ROI包含背景/加载动画，持续运动阻止授权補点；并非耗尽了20次重试。

用户指出模拟器VPN必须开启后，只读观察证实tun0不存在，vpn_management只有默认Legacy VPN/type=-1，没有活动Clash网络；电脑suyou-mihomo进程不能证明安卓游戏经VPN。旧EnsureClashVpn及新版DeviceSession::execute_lifecycle(EnsureVpn)均已实现已连接检查、Clash启动、系统授权及连通后切回游戏；问题是AUTO_START_CLASH=false，正式链据此跳过恢复。按用户授权由profile CAS仅将该字段改为true，旧profile envelope保存在`next/.local/resume77-930/profile-before-vpn.json`，新revision为f31710ea57ed823580030af7389d4f9af2c8ab158188225166df7ea051cbddb4。

candidate36 run38实际执行EnsureVpn，生命周期确认vpn_ready=true，安卓tun0 UP、活动包com.github.metacubex.clash.meta/type=1；随后切回游戏。该轮312.61秒Completed、战斗1、报告1、住宿提交1。原59轮完整结果保留，剩余41轮；完成后通过正式停止接口取消轮间续跑，正常释放设备，再替换后台。

boot.cpp将动作计时包装的非输入max_hit改为0，由既有无进展窗口约束；网络RetryZhHant/En授权改为prompt，去掉region_quiet条件，3秒间隔新帧复核正文/按钮/坐标，保留120秒首次结果期限与180秒处理期限，不重发父业务输入。旧Python启动重试按1.5秒间隔直到120秒，未设“两次网络错误即退出”；此次恢复相近数量级的重试能力，不复制固定中心点击或未知页面盲点。

只构建automationd和test_native_author。--boot-progress的连续3次独立网络错误、背景运动仍补点、已知动作等待、迟到转场及未知页期限5项通过，证据在`next/.local/logs/network-retry-vpn-930-*.log`；后两项新增用生产图和执行器、隔离场景叶子，没有连接正式游戏制造故障。git diff --check通过。

candidate37已部署，EXE 6319227b480fd7b4fe8300f8a39f6eb1d8cbaad1b9390cb80eec6bceb15182cc，包revision保持baf41851d15a4db35342fbb2cf0c0b52a8474e9d3b0a4b21d4280eb898e978fd。后台PID50596，实例46E8F2F9-3B8A-4A84-BF30-0D41C8DBDAA3，41轮请求4bc79436-36c5-4b08-bd31-7605a76315f0；22:44确认Running且已离开悬赏检查页继续任务。EnsureVpn观察已连接并skip，未反复开关。候选尚未完成整轮或自然反复网络错误验收，不把构建/窄检查当实机长期通过。未commit/push，未清理历史结果、用户配置或mod。

## 悬赏页ROI与页面身份修复

candidate37随后完成3轮，run4在431.22秒因`FLOW_INVOCATION_TIMEOUT:Task_BountyReport_Entry`停止，总计完整62/100。公会公共链已经点击悬赏页签(790,1411)，真实画面有“懸賞令列表”和“達成報告”，但没有提交报告输入。选中态素材横跨x=709到825，旧ROI `[590,1370,210,90]`右界800裁掉文字，最高分0.4341；整条页签栏0.9893。错误在后置页面确认，并非触摸未送达、漏切悬赏或缺报告素材。

candidate38先将入口与选中态搜索扩大至整个横移页签栏。实机run1正确提交报告、显示领取回执并点击关闭，但根任务仍失败：关闭回执后页签栏自动回到左侧，选中“悬赏”页签离屏，页面仍为悬赏列表。强制要求选中页签命中，即使ROI覆盖整条栏也不可能满足。实际奖励已领取，报告计数0、pending=true是后继业务确认未完成，不能伪造为成功或重放原送达不明输入。该失败不计完整轮次，原结果和时序保留。

当前修复按职责分开：

- `guild.bounties.entry`在整条页签活动区域定位，仍按真实匹配中心点击，不固定旧坐标。
- `guild.bounties.selected`保留为独立可见页签观察，既不删除素材，也不冒充页面身份。
- `guild.bounties.page`要求专属标题、已加载WANTED卡片、委托页标题反证，不要求可离屏的页签。公共开页、领取关闭回执、报告结清及退出列表共用该配方，避免只修单个点击。
- 不降低阈值，不增加盲点，不将所有ROI改成全屏。固定标题可以收紧，横移控件按完整活动区域；“可滚出屏幕的控件”不能充当必需的页面身份证据。

只核对相关真实帧和现有窄入口：正式Service对领取后真实悬赏帧Hit、真实委托页NoHit、显式合成仅标题负例NoHit；Report/Reveal图校验通过。最初从错误cwd `next/resources`运行该入口导致JSON路径为空，未记通过；改回要求的`next`后通过，两个日志均保留。没有执行旧全量矩阵，也没有把离线结果当作实机完整通过。

证据：`next/.local/network-retry-vpn-930/board-current.png`、`board-roi-evidence.jsonl`；candidate37 run4诊断PNG；candidate38实例`90481634-8C6D-49C3-B133-857E22F6EBB6`的run1时序/结果；领取后页签离屏帧`next/.local/c11-flow-product/data/recent-frames/20260930T152635315_r1_f44.jpg`；窄检查`next/.local/logs/bounty-page-final-930-check-correct-cwd.log`。

candidate39构建并部署至17654，PID10540，实例`B12D219E-2182-43DE-AF84-A586D0BCC488`，EXE `91bf1a313402508dfe7b67aaa1aa0e94503bfe919856df37164b43749fbba447`，资源包`118d6be913ab2092567cdc5d17aa23a9106a9a7b245b61f148d40f283025ebb0`。部署前确认candidate38 Failed、静止、结果落盘、循环关闭、设备断开；复杂强制终止/启动合并命令被工具拒绝，未执行，随后正常停止指定旧工具进程并分步启动新候选。游戏、模拟器、VPN和用户配置未关闭或覆盖。剩余38轮请求`844731bf-8c93-4588-9970-f89e3ecaf5e9`已正式接收，run1从页签离屏的悬赏列表收敛退出、跳轮并导航到战斗；整轮结果尚在观察，不将Running或局部动作计入完整62轮。未commit/push。

最终实机复核：candidate39 run1已Completed，3/3业务段、quiescent=true、result_saved=true；报告1份，200G住宿提交1次、inn_rest_completed=true。Report输入6617.45ms后确认，CloseReceipt在282.66ms后确认，ReportConfirmed正常结清，随后退出列表、住宿并回城。新会话completed_cycles=1，run2自动开始且repeat.active=true，正式累计63/100、余37轮。最小报告输入证据保存在`candidate39-report-proof.json`，运行快照`resume38-final-running.json`；完整原始结果和时序保存在该实例run1。没有把candidate38局部领取算为完整轮次，不把一次通过扩展成其它ROI或全部异常已验证。
