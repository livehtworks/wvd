# 2e17e6e关键修复合并结果

日期：2026-10-02。仓库：`livehtworks/wvd`，分支：`agent/local-stability-notes`。完整基线：`2e17e6e2f20e2590dc2a33d9dd00b50bda8a516c`。

## 结论与范围

按工作包执行A01–B05正确性及交付收束，未执行P01–P04性能生产修改。M01仅分析既有数据，保持`MEMORY_UNATTRIBUTED`。工作包阶段没有连接设备、运行ADB、启动/关闭模拟器或游戏、切换VPN、真实截图/输入、部署、循环、commit/push；正式配置、保存流程、历史日志、旧dist和用户`.vscode`未修改。

2026-10-02用户在交付后明确追加commit/push授权。此次仅提交本报告、当前事实/执行注意项及相关源码和窄测到个人远端`fork`的`agent/local-stability-notes`；没有新增验证、构建、部署或游戏操作。原候选保留构建时的dirty源码身份，提交后不篡改旧回执，不冒称产物来自新提交的重新构建。

不是“整体游戏验收通过”：本轮证明的是受影响Release目标可构建、真实生产执行器的受控时序契约、隔离作者准备入口与候选预检。应用器原套件有一项环境权限阻断；内存仍未归因，均保留如下。

| 项目 | 最终状态 | 证据结论 |
|---|---|---|
| A01/R1 | APPLIED、BUILD_PASSED、OFFLINE_PASSED | 原声明/lowering/append/序列化一致；真实take_turn生成图19个保护输入；五种重启中断情形保留原pending且不重发 |
| A02/R2 | APPLIED、BUILD_PASSED、OFFLINE_PASSED | 首帧失焦/transport失败均force一次、恢复检查两次、Boot一次、父Prepare一次；同窗口重启事实保留 |
| A03/B1 | APPLIED、BUILD_PASSED；函数及原生OFFLINE_PASSED；应用器一项BLOCKED_ENV_PERMISSION | source_identity 12/12；应用器19/20，另1项夹具权限错误，不冒充20/20 |
| B02/R3 | BUILD_PASSED、OFFLINE_PASSED | 13个pending场景与6个原声明只读场景；新帧原回执优先于到期重启，硬总期限/停止有效 |
| B03 | BUILD_PASSED、OFFLINE_PASSED；部署NOT_RUN | 构建输入冻结；四种错误前置拒绝，完整候选只Validate；替身旧服务查询/退出均0 |
| B04 | ROOT_CAUSE_CONFIRMED、BUILD_PASSED、OFFLINE_PASSED | 同一原保存文档/revision隔离准备通过，非法字段明确拒绝；设备调用0 |
| B05 | OFFLINE_PASSED | 49轮原11字段完全一致；真实生产hook写出合成PNG，缺帧/写盘失败不记完整 |
| P01–P04 | NOT_RUN | 没有更改角色候选、固定等待、ADB元数据或组合识别语义 |
| M01 | MEMORY_UNATTRIBUTED | 147个既有资源事件的生命周期边界不足以解释OS私有提交增长 |
| 实机/长期资源验收 | NOT_RUN | 不复用历史100轮作为本候选验收，不制造故障或长测 |

## 补丁应用与数据保护

原ZIP解压在`next/.local/critical-workpackage-20261002/WVD_2e17e6e_CriticalFixes_20261002`，下称`$Package`。实际仓库根下称`$Repo`，不用本机绝对安装路径作为产品默认值。

应用器先核对完整HEAD、八个目标blob、暂存区、工作树及唯一锚点，`--check`输出`READY 8`，再`--apply`。回执、原文件和标准补丁保存在`next/.local/critical-2e17e6e-20261002T034252Z-b03b9b32/`。此回执只证明安全应用，不证明编译/游戏运行。最终暂存区保持空、HEAD未改变。

所有会写数据的验证使用新TEMP目录或本轮独有`.local`子目录。B04只读复制原作者文档与profile后走隔离WorkflowRepository/Application准备链；未调用Session.start。候选预检替身HTTP服务使用系统分配端口0，非17654，没有打开正式目录。原始运行日志/截图没有纳入源码。

## 代码变更

### A01：技能中断保护

`workflow::Input`新增可选`interruption_reason`，来自原`PipelineCompiler::stop_if_interrupted_after`声明，经lowering和序列化保留；不是新技能白名单，FlowProgram schema仍为7。append保留同一原因。

已证实应用重启后，`FlowExecutor::retry_observation`先遍历所有活动调用帧pending，存在保护标记则保留原输入身份、次数、账目，以原原因ExternalBlocked；不先清pending、不用重启后的战斗页面反证旧技能成功。普通菜单、跳轮、金币、未知送达及effect行为由原窄测保护，未恢复旧三次上限。

真实`take_turn`通过正式PublicStepScope装配实际公共定义，验证正常技能确认、防御及确认、友方选中及确认五个直接节点，并核对公共步骤保护，共19个输入；每个标记都与源声明逐项一致，普通输入未加标记。五种执行器保护场景只提交一次，原epoch/attempts/来源保留，无confirmed。

源码读取点核查未发现另一个反序列化FlowProgram的活动入口；不声称完成不存在的反序列化兼容。旧发布缓存不补写，新运行必须重新编译/发布源图。

### A02：单次重启意图

仅在适配器返回真实`application_restarted`成功事实后清`restart_application`，Boot由原`restart_handler_pending_`保留。恢复未返回时不提前消费。读取恢复快照分开记录最近`context_recovery`与累计`application_restarted_in_window`，避免最近一次只读恢复为false覆盖本窗口已重启事实。

只读审查DeviceSession：成功force-stop后立即保存既有阶段标志，后续查询/捕获抛错时仍保留，直到成功回执构造后才清除，未再添加恢复协调器。两个生产执行器替身故障场景均验证force=1、恢复检查=2、Boot=1、父Prepare=1，不混淆应用重启与连接代次。

### B02：迟到回执与重启竞态

抽取`settle_await_result`，普通Await、局部阶段到期且既有异常宽限有效、强制重启前共用唯一结算路径。先遵守停止/硬总期限；同一活动Await的新帧须与原pending证明关联，设备、连接、视口/旋转、frame_id和action_epoch不匹配不能结清；识别Error原样失败。

未知送达须真实后置Hit且输入通道清理成功，清理后再次检查停止/硬总期限，才记录一次confirmed、结束连续输入计数、释放pending并交接后继。局部到期不改submitted_at、首次结果预算或再次发送；只有真实结清后开始后继选路阶段。

重启阈值先保留Overlay处理，再核对当前Await，不跨事件帧确认父业务。无pending时仅查原阶段已声明Observe或ongoing，不做全局页面穷举。Observe的guard与实际request都须命中；事件处理器按钮消失不清父异常。子调用handoff的迟到Observe被选中时清理旧returned_targets，避免正常后继再走旧出口；ongoing只说明继续等待，不表示完成，也不刷新阶段期限。

`--late-receipt`覆盖局部迟到、阈值恢复、真黑屏、旧帧、错代次、Error、未知送达、通道清理失败/清理期间停止与总期限、普通停止与总期限、异常处理器父子隔离，共13个pending场景；另覆盖当前Observe、原后继、子调用返回后继、ongoing、读取Error、仅guard命中6个场景。没有第二次业务提交或重复结算。受控测试使用缩短窗口，不修改生产60/180秒及总墙钟规则。

### B03：构建身份与只读候选预检

`source_identity`使用相对HEAD的净工作树及未跟踪源码内容，全部暂存也保持dirty；不把`.vscode`/运行产物作为源码。原12项函数测试通过。

构建入口先资源同步/配置，再`begin_build`，web/native结束后`finish_build`核对同一源码身份并记录实际EXE、capture-host、锁定DLL、scrcpy和完整web产物哈希。stage前后再次校验，不接受构建期间源码变化或构建后产物变化。

管理脚本复用唯一`Assert-Candidate`，新增仅`Validate`入口：标记、源码/构建身份、全部候选成员、必需文件、web/pack清单、构建产物和EXE `--version`先验证，再允许原Start/Deploy链读旧服务/申请锁/发送shutdown。本轮只Validate，没有执行Start/Deploy/Stop；生命周期所有者、创建时间核对及数据锁语义未改变。

隔离检查包括源码中途变化、必需capture-host缺失、EXE哈希错误、pack成员篡改及完整候选。错误均在旧服务查询/退出前拒绝，完整候选校验不部署，DataRoot和service-launch未创建。

发现并修复Python子进程继承PS7模块路径使Windows PowerShell 5.1找不到`Get-FileHash`的问题：新候选预检改用标准.NET SHA256流，不调整全局环境。保留首轮失败日志和最终成功证据。Windows `.ps1`保留UTF-8 BOM。

最终冻结回执为`next/.local/build-input.json`，独立候选为`next/.local/critical-workpackage-20261002/candidate-release`，身份副本为该候选`DELIVERY_STATUS.json`的`build_input`及源码字段，状态仍为`BUILT_NOT_GAME_ACCEPTED`。身份不能在源码最后改动前手填；以最终冻结、产物校验和候选记录为准。此前中间candidate/candidate-preflight保留，不作为当前交付或部署目标。

### B04：原独立作者入口

找到同一`live-scorpion-combat-20260925`保存文档，revision为`69a4c81257369e52a7040e96426ce177334b71a7c168638609d004129ac5c16b`，原文件SHA256为`ded715bd0cfda24df439f2e0a600e2c91ccf7b8ba7dc4cb7c7dd3b36f541a496`；profile SHA256为`93e9ec76b6e6e709ca9ab221a92ea23ea1b41cbcc8183bc81010d43b290c2f38`。原文档/profile未改写。

最早异常在Release带符号的隔离准备链捕获：`PublicFlowLibrary::compile`原第117行 → JSON `iter_impl::operator*` → `invalid_iterator.214`，上层为`Application::assemble_workflow`。问题路径是原生编译结果的`workflow.authoring.source_paths`，相邻`resources`同样受影响；源码生成契约为JSON object映射，不是用户文档某字段缺失。C++20 range-for对`value(...).items()`借用已销毁临时对象。将两处临时JSON保存为命名const值后再迭代，未用catch跳过结果。类型/生命周期由对应生成和使用源码核查，异常栈保留原行号，不声称异常栈包含堆内变量快照。

原文档随后暴露`FLOW_HANDOFF_BINDING_MISSING:Entry:Task_Author_battle:blocked`：内置battle有blocked/chest/revive出口，独立作者业务节点仅接success。仅对业务节点把未接出口转交调用者同名handoff；显式绑定保持，普通公共call缺绑定仍拒绝，根未接出口仍ExternalBlocked，不伪装Completed。

同一revision与实际公共依赖闭包通过原WorkflowRepository/Application装配及native发布准备，units=1、root handoffs=blocked/chest/revive。复制文档中`/nodes/0/parameters/binding`改为number，正式创建入口明确报`AUTHOR_BUSINESS_PARAMETERS_INVALID:battle`；两种检查连接/capture/input均0。没有删重建正式流程。

### B05：分析与诊断

实际只读分析来源为`next/.local/menu-restart-20261002/analyze_candidate63.py`，整理到`next/tools/analyze_run_timing.py`，必填`--runs-root`及`--output-dir`。保留原11字段统计、failed/unknown/rejected与exclusive口径，不把worker工作量、输入耗时及节点墙钟叠加。

同一实例D22CE3AB的run1–49，重新分析平均232.666秒，原11字段逐项完全一致；输出每个输入文件SHA256及日志身份。拒绝无数据、缺文件/截断、行数不符、混program/pack/instance身份及输入输出重叠。历史日志未记EXE哈希，`binary_identity=NOT_RECORDED_IN_RUN_LOGS`，不能完全排除相同程序身份下替换二进制。6项工具窄测使用独立临时目录，全部通过。

重启保存hook抽取为Coordinator小型私有函数，原生产调用点/条件不变，不额外抓图。合成900×1600 BGR=123且身份完整帧，通过同一hook及RunStore真写PNG，校验尺寸/像素/哈希/帧身份，`stage=recovery_entry`、`evidence_kind=before_application_restart`；缺帧及写盘失败均complete=false，原业务错误`ORIGINAL_BUSINESS_FAILURE`不被覆盖。未放宽RunStore准入，未改历史candidate62诊断。

## 实际命令、退出码与证据

以下构建/原生命令工作目录为`$Repo/next`，Python提供包命令以仓库根执行。日志统一在`next/.local/logs/`，不提交。测试程序实际位于`build/native/Release`，不是工作包示例的`build/Release`。

| 命令/调用 | 退出码 | 证据文件或结果 |
|---|---:|---|
| `python $Package/APPLY_PATCH.py --repo $Repo --check`；随后`--apply` | 0、0 | `.local/critical-2e17e6e-20261002T034252Z-b03b9b32/receipt.json`、`applied.patch` |
| `python $Package/tests/test_source_identity.py --source $Repo/next/tools/package_functional.py -v` | 0 | `critical-source-identity-final-20261002.log`，12/12 |
| `python $Package/tests/test_patch_installer.py` | 1 | `critical-installer-20261002.log`，19通过，1环境ERROR |
| `python .local/critical-workpackage-20261002/check_installer_junction.py` | 0 | `critical-installer-junction-20261002.log`，生产应用器拒绝junction，两个原文件未变 |
| `cmake --preset windows-x64 -DCMAKE_CXX_FLAGS_RELEASE="/O2 /DNDEBUG /Zi" -DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG` | 0 | `critical-debug-configure-20261002.log`，仅本机构建缓存带符号，不改依赖 |
| `cmake --build --preset windows-release --target automationd test_native_flow test_native_author test_native_application test_native_coordinator` | 0 | `critical-release-build-20261002.log`，最终受影响Release目标 |
| `build/native/Release/test_native_author.exe --critical-input-protection` | 0 | `critical-A-real-turn-20261002.log` |
| `build/native/Release/test_native_flow.exe --critical-recovery` | 0 | `critical-final-flow---critical-recovery-20261002.log` |
| 同入口`--closure-recovery` | 0 | `critical-final-flow---closure-recovery-20261002.log` |
| 同入口`--exception-restart` | 0 | `critical-final-flow---exception-restart-20261002.log` |
| 同入口`--selection-race` | 0 | `critical-final-flow---selection-race-20261002.log` |
| 同入口`--timeout-reclassification` | 0 | `critical-final-flow---timeout-reclassification-20261002.log` |
| 同入口`--instance-start-window` | 0 | `critical-final-flow---instance-start-window-20261002.log` |
| 同入口`--late-receipt` | 0 | `critical-final-flow---late-receipt-20261002.log`，13+6场景 |
| `test_native_application.exe --critical-author-entry packs/wvd ../resources/quest/quest.json <原文档> <原profile>` | 0 | `critical-author-entry-fixed-20261002.log`；原路径在c11-flow-product/data，只读来源 |
| `test_native_coordinator.exe --restart-diagnostic` | 0 | `critical-diagnostic-final-20261002.log`，3种落盘情况 |
| `python tools/analyze_run_timing.py --runs-root .local/c11-flow-product/data/runs/D22CE3AB-1E48-40A0-BF82-F5AD42F3FE4C --output-dir .local/critical-workpackage-20261002/analysis` | 0 | `critical-timing-analysis-20261002.log`；`critical-analysis-equivalence-20261002.log`无不同字段 |
| `python tests/test_run_timing_analysis.py -v` | 0 | `critical-analysis-tests-20261002.log`，6/6 |
| `python .local/critical-workpackage-20261002/analyze_memory_boundaries.py` | 0 | `critical-memory-existing-20261002.log`；`analysis/session-resource-samples.json` |
| `package_functional.sync_authoring_resources(); begin_build()`；冻结后`npm.cmd run build`；上述Release构建；`finish_build()`；`stage(<新的candidate-release>)` | 0 | `critical-release-freeze-20261002.log`、`critical-release-web-20261002.log`、`critical-release-build-20261002.log`、`critical-release-stage-20261002.log`；最终DELIVERY_STATUS记录身份 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/service_lifecycle.ps1 -CandidateRoot <candidate-release> -PreflightOnly` | 0 | `critical-release-preflight-20261002.log`，生产manager仅Validate，四种拒绝与完整候选，旧服务HTTP调用0 |
| `git diff --check`；`git diff --cached --name-status` | 0、0 | `critical-diff-check-20261002.log`；暂存区为空 |

首个B04失败保存在`critical-author-entry-first-stack-20261002.log`，修复前后不抹掉失败。打包PS5.1模块失败在`critical-candidate-stage-20261002.log`及`critical-preflight-python-20261002.log`，修复后完整预检通过。开发中的补充fixture曾分别遗漏Return端口声明、把进入读取恢复误算正常恢复，已按真实契约修正；最后上述19个场景通过，没有修改生产成功条件来迎合夹具。误用不存在的build preset被拒绝后改用既有`windows-release`；不计失败命令为成功验证。构建仍有已有/夹具的unused参数、shadow等非致命警告，不称无警告构建。

PNG隔离证据位于TEMP下`wvd-native-coordinator-*`；原作者准备证据位于TEMP下`wvd-author-entry-*`，具体本机目录只记录在对应本地日志，不写入产品路径。参数化分析和本轮私有补充检查不产生正式运行事实。

## 环境阻断、未执行与未归因

原应用器`test_symlink_target_refused`在`Path.symlink_to`创建文件链接时抛`WinError 1314`，未进入应用器。没有跳过/删除/放宽测试、修改系统开发者模式或权限。目录junction补充检验覆盖Windows reparse拒绝，但不能称原文件symlink夹具通过；其状态保留`BLOCKED_ENV_PERMISSION`。不影响真实八文件安全应用记录或原生修复结果，禁止声称全部测试通过。

M01：49轮中147个`recognition.resources`事件，cache_retained/live为3,411,558–4,543,716 bytes、cache_in_use及active_matches为0、result_cache估计1,771–3,720 bytes；对应边界private为207,544,320–447,094,784 bytes。全程周期内存样本有回落，不能将单段斜率外推泄漏速度。资源事件在Session.run后、局部Session/recognizer/恢复帧持有者释放前，不是join及全部所有者释放后的OS基线；缺少对齐句柄/持有者计数及分配栈，几MiB缓存不足以解释全部增长。

因此保持`MEMORY_UNATTRIBUTED`，不把迭代器修复当内存问题已解决，不承诺OpenCV无泄漏，也不依据cache_in_use=0推断OS已返还全部堆页。后续若需要所有者释放后的有限记录或隔离堆栈差分，另按M01方案授权；本轮未加监控线程/高频采样、清工作集或周期重启。

实机菜单/技能/NEXT/Pause、自然网络故障、运行中模拟器退出及长期资源稳定性为NOT_RUN/NOT_OBSERVED。没有全量ctest、旧矩阵或几十小时压力测试。候选仅构建与预检，不切换生产、不延续循环、不自动commit/push。
