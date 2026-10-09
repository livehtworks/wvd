# 运行问题汇总与暂停交接

## 本次范围与现场

2026-10-09 23:05（北京时间）按用户要求停止继续修复，只整理问题、保留已有源码改动并提交个人远端。不新增策略、不构建、不部署、不操作游戏、不重新启动循环。本报告不是稳定性通过报告。

- 当前服务为 candidate138，原端口17654、原data；最新API为 UserStopped、busy=false、quiescent=true、repeat.active=false。
- 请求1953ee22-7a5b-4c7c-a21f-2b9339c79408，协调器C37C2A8F-4A66-4D1F-B744-03AB8E8252EA；本批8/41完成，第9轮UserStopped，不计完成。
- 停止节点为Task_FirstDungeon_Battle_Actor_SharedStep3_Author_target；停止不是本轮调查主动重启或新识别错误。
- 此前累计59轮，加本批8轮为67/100。这是跨候选、跨恢复的累计，不是连续稳定100轮；几百/几千轮的长期目标尚未证明。
- 正式配置、运行日志、mod和用户.vscode不纳入本次源码提交。当前运行候选不因Git提交而自动变更。

## 未解决问题

### 1. 迷宫恢复识别放大，简单动作耗时约46秒

证据来自本批第8轮，而不是合成测试。该轮业务Completed，但完成不等于性能正常。

按action-timing.jsonl中type=timing.segment、payload.node_id包含Heal的段汇总：wall_ns合计46352148700，约46.352秒；actual_matches合计1199。包括开头一次无需恢复的Checkpoint约0.279秒及极小的父调用段，不能将汇总数称为某一次单击耗时。

| 类别 | 本次耗时 | 说明 |
| --- | ---: | --- |
| 像素采集与转换 | 0.334秒 | pixel_capture 310.40ms、pixel_convert 23.92ms |
| 模拟器/截图元数据 | 6.197秒 | capture_metadata |
| 识别及识别调度合计 | 34.906秒 | match 2.830秒、parallel_wait 2.114秒、recognition_other 29.962秒 |
| 显式等待 | 3.569秒 | explicit_wait，不是主要耗时来源 |
| 输入验证 | 1.255秒 | input_validation |

剩余为日志、提交、其它调度与未归类时间。worker_exclusive_ns.match另有约2.045秒，它明确不能叠加到主线程墙钟；本报告没有把并行工作时间重复相加。recognition_other还包含识别组织/调用开销，不能说34.906秒全部是OpenCV单次匹配计算。1199来自实际比对计数，不是断言数量、不同素材数量或游戏点击数量。

| 恢复段 | 墙钟毫秒 | 实际模板比对 |
| --- | ---: | ---: |
| Entry（需恢复） | 1867.9 | 43 |
| Requested | 3928.3 | 86 |
| Begin | 3679.0 | 162 |
| Open0 / await | 2165.7 / 5943.2 | 58 / 124 |
| Trait / await | 2746.1 / 3944.5 | 57 / 72 |
| Ready | 4363.3 | 61 |
| Recover / await | 2419.2 / 7095.0 | 56 / 319 |
| Back0 / await | 1999.1 / 3599.3 | 56 / 74 |
| Recovered | 1941.5 | 31 |

源码调用链：

- next/native/games/wvd/supply/dungeon_recover.cpp:12-19：interrupted组合战斗、宝箱和再起；dungeon、panel、context、post层层复用并包含这些组合。
- 同文件:29-30：Requested与Begin先后重复证明context；输入前、后置和候选分派又使用这些条件。
- 同文件:37-60：打开角色、特性、恢复、返回都使用宽泛post，再回候选列表进行判断。
- next/native/games/wvd/vision/native_recognizers.cpp:1234-1258：all/any组合会评估所有子项；即使已有足够布尔证据，也继续检查其余分支。当前注释明确要求不短路，以避免掩盖Error。
- 不同观察周期会重新取帧；同帧叶子缓存不能消除跨帧的重复组合检查。不能为了压低比对数恢复无限旧帧复用，否则会重新引入已确认的导航空转缺陷。

已确认的问题是恢复链识别规模与操作复杂度严重不相称；图像获取并不是本次主要瓶颈。还没有完成逐个子识别配方的耗时/分配归因，也没有证明其它流程不存在同类放大。不能将这一例外推成所有节点都做1199次比对。

**本次未修复**：dungeon_recover.cpp与native_recognizers.cpp在当前工作树相对HEAD均无改动；没有把通用all/any直接改成短路、没有缩ROI、没有降低阈值、没有增加新等待或重试限制。candidate138未修复本问题。

### 2. 正常页面和业务分支曾被误当异常，既有修复仍需长期证据

此前实机出现过以下不同缺陷，不能合称网络问题：

- 已选节点的条件在执行前失效，却持续死磕该节点，不重新选择同层正常分支。
- 正向观察/子返回循环复用旧帧，返哈肯无路线反复命中751次，最终FLOW_INVOCATION_TIMEOUT；诊断图提交时已过时213秒。
- 导航动作选中到执行之间已到哈肯，公共步骤没有先进行只读交接。
- 战斗有行动条但暂时无菜单的动画/结算阶段，被累计为未知异常。
- 读取恢复已花约74秒重新启动实例，旧异常计时没有清理，紧接着又执行游戏重启。
- 前台切到VPN的诊断像素被错误拒绝，图片诊断不完整又阻断已经完成的业务续轮。
- 模拟器退出时存在旧技能未确认回执，协调器在生命周期恢复之前退出。

这些已有源码修改、有限针对性验证和部分实机续行证据；并非本次才提出。对应交付分别见下方索引。尚不能宣称异常恢复全链或任意次数循环已稳定。

### 3. 单击送达后普通菜单漏同页补点

candidate137第二轮郊外“回到城鎮”点击已接受，但原按钮一直在，attempts=1，60700ms后程序主动重启。后续另有nvoglv64.dll/901崩溃，先后关系不能倒置。

candidate138补入统一retry_menu_input，并同步6份正式公共定义的17个节点；菜单6种有限场景及原输入保护检查已通过。本批前5轮无主动游戏重启/读取恢复，但这不证明所有菜单和所有网络条件都已闭合。迷宫恢复本次46秒问题没有被这些菜单修改解决。

### 4. 模拟器自然崩溃仍未归因

candidate138第6轮 Windows Application事件1000：2026-10-09 22:19:22.9707331+08:00，MuMuNxDevice.exe PID45736，libRenderer.dll，异常0xc0000409、偏移0x929299。工具随后重新启动实例并恢复，读取恢复约68秒，该轮最终Completed。

这次没有先出现CONTINUOUS_EXCEPTION_TIMEOUT或diagnostic.application_restart，不能与此前正常页60秒误重启混淆。先前其它轮次还存在nvoglv64.dll/901签名；两种直接故障模块不能合并归因。

尚缺崩溃分配失败栈、厂商内部渲染证据或可控复现，不能断言是OpenCV泄漏、VPN切换或某个SDK调用。新旧nemu截图ABI一致；新capture_host每帧查询分辨率、旧Python连接时查询，是待核查差异而非已证明根因。未修改驱动、模拟器图形设置或截图SDK实现。

### 5. 内存来源没有完全闭合

同一candidate138进程，本批第1/8轮相同收尾阶段的memory-lifecycle.json：

| 边界 | 第1轮 | 第8轮 |
| --- | ---: | ---: |
| worker_joined private_bytes | 100347904 | 116215808 |
| heap_resources_optimized private_bytes | 34484224 | 37830656 |
| 优化后可枚举堆allocated_bytes | 18737249 | 18880327 |
| 优化后句柄 | 320 | 320 |

第9轮用户停止、批次释放后private_bytes为43053056，句柄319；它不是正常完成轮，不替代第8轮同阶段比较。第6轮崩溃前工具约551.9MiB、系统提交56.02/60.03GiB；第8轮OCR destroy.begin/end记录私有提交596176896→159453184，说明该实例销毁释放了一部分，但不解释所有常驻增长。

堆allocated、堆内部commit与PrivateUsage含义不同，不能简单相减证明泄漏位置。上述结果没有给出全部增长的分配调用栈；仍保留RESOURCE_UNRESOLVED。日志可区分阶段、对象所有者和释放边界，却不能代替缺失的分配归因。

此前全系统VA采集容量提前失败、事件丢失、堆工具枚举不全等限制仍见历史内存报告及execution-notes；本次未再开WPR/CDB/堆跟踪、未延长采集、未跑新的内存测试。

### 6. 取证和验证边界

- 历史动作图是240张/128MiB滚动留存，本次查找未找到第8轮恢复对应图片；时序/计数日志仍在，但不能凭文字日志重新构造真实画面。后续审核需区分有原图与仅有事件证据。
- 有限生产执行器检查、构建通过、单轮完成，不等于完整实机长期稳定。本次没有重跑测试或追加数量。
- 缺繁中素材的普通随机迷宫事件仍受既有任务依赖准入约束；多人死亡按用户要求停留人工处理，不制造死亡补样本。
- 绿色/紫色宝石购买仍禁止；金币住宿允许不代表可以伪造付款或业务回执。

## 提交范围

保存此前已完成但尚未提交的实例退出恢复、诊断分类、观察周期、正常分支重选、目标战身份恢复、战斗无菜单等待、菜单补点及相应验证源码和资源。不是将未修复的恢复性能问题混入“已完成”变更。

不提交正式profile、正式保存定义、截图/日志原件、候选EXE、用户.vscode。正式保存公共定义之前通过备份及revision比较交换更新，源码和包内public-flows同步，详细收据仍在本机忽略目录。本次不继续更新这些定义。

## 审核入口与原始证据

- [实例退出核查](giant131-instance-exit-20261009.md)与[恢复修复](instance-exit-recovery-repair-20261009.md)。
- [重复重启核查](repeated-restart-investigation-20261009.md)与[前台/计时修复](context-recovery-closure-20261009.md)。
- [导航旧帧与公共交接](navigation-read-cycle-repair-20261009.md)。
- [整链审查](flow-dispatch-chain-audit-20261009.md)与[分支重选交付](flow-dispatch-repair-20261009.md)。
- [菜单补点及正式定义同步](menu-input-retry-audit-20261009.md)。
- [本次MuMu渲染崩溃](mumu-renderer-crash-20261009.md)。

原始目录为next/.local/c11-flow-product/data/runs/C37C2A8F-4A66-4D1F-B744-03AB8E8252EA/，保留本机、不进入Git。文件SHA256：

| 文件 | SHA256 |
| --- | --- |
| 8/action-timing.jsonl | 04F7DD0B114BF2EA31FDBBC690DB8954A48EEAE68ACDA4941BD3F8D15A0B7A77 |
| 8/execution-events.jsonl | 4E7537471F152C6CEFA5BE87D0497F1117AA0D1288E6AF18BC179D68D5DCF65F |
| 9/result.json | A177EE948EE67483646A9942FE432B59AA19535EB050AA52C4E3B6914E10FB3A |

结论：本次整理与源码保存不解决恢复识别放大、模拟器崩溃或完整内存归因；循环保持停止，后续整改范围等待用户审核决定。
