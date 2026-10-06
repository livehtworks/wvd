# 巨人循环仅堆快照窗口

## 范围与身份

用户先要求继续50轮巨人，再要求只监控两轮后关闭采集。首次全系统VA采集提前失败后，用户明确另行授权新窗口：只采目标进程堆快照，最多20分钟、跟踪文件合计512MiB，当前轮为部分采样，下一轮完整；游戏50轮不停止。不提交额外任务，不改生产源码、部署或用户profile，不制造异常或消费宝石。

- 产品：candidate121，PID38436，创建FILETIME `134357701182075891`，服务实例 `6DD72FBD-D995-4900-9370-19EECB401B7B`。
- 循环：请求 `01a4410d-4c24-4cc4-9ad1-3bcaeedb8193`，协调器 `8A830897-80B1-4F54-A1DF-BDFED9A2D0D6`，目标50轮；只观察自然Completed/quiescent/worker_joined。
- 新证据：`next/.local/memory-monitor-heap-two-cbdf2cc6-4ba7-43d7-8013-8088c8d32eab/`；helper PID46052，独有会话 `WvdStacks_5842a42cab7248cfb0211ba446d25511`。
- 新范围：`Monitor -CaptureKind HeapSnapshots -MonitorCurrentRound`，零额外提交；2026-10-07 01:34接入，当时完成5轮、run7正在运行。deadline UTC `2026-10-06T17:54:05.1390594Z`（本地01:54:05）。
- 采集与导出profile在证据目录冻结，匹配PDB经GUID/Age/SHA核对；Heap快照只覆盖启用后的分配栈，不含VirtualAlloc。首次管理员重试曾被工具拦截，未启动新helper；收到用户新范围授权后才重新启动。

## 前一个窗口失败

证据：`next/.local/memory-monitor-two-6ef8b9fe-ee28-4d18-959a-3da314fe30af/`，独有会话 `WvdStacks_596a6e373d4f46f8aa736241b937113a`。全系统VA内核Sequential文件达到100663296字节（96MiB），提前保存阶段仍因日志封顶失败（0x800705de）。回执complete=false、elapsed=160.594秒、rounds_completed=0、cleanup_confirmed=true；没有获得worker_joined窗口，不能用残缺原始文件算有效分配差分。

这是空闲probe通过后暴露的实际吞吐问题，不能用空闲5.55秒采样证明游戏窗口能完成。失败没有停止50轮，也没有自动重启跟踪；原回执及ETL保留，不回写成成功。旧工作包437MiB/136897丢事件属于另一个历史窗口，同样保持失败。

## 本次结果

本次采集已结束，**两轮窗口未完成，内存归因未完成**。没有自动续开第三个窗口。

| 项目 | 实际结果 |
| --- | --- |
| 耗时与截止 | 1196.967秒（19分57秒），在本次20分钟内结束；failure=`TIME_LIMIT_CLEANUP_RESERVED` |
| 快照 | 01:34:10的`attached_partial_window`，01:41:37的`worker_joined_1_partial`；缺第二个join |
| 轮次 | 额外提交0、观察到启动2、完成收尾1；当前轮run7是部分采样，run8未在窗口内收尾 |
| 实际收尾 | run7 Completed/quiescent，批次6/50；worker_joined私有提交124530688字节、句柄366，Session/OCR/Service live均0 |
| ETL | `incomplete.etl`，43845188字节（41.814MiB），WPR明确保存成功；不改名为完整采集，不回写receipt.complete |
| 丢失与清理 | 586个健康采样，两个采集器EventsLost/LogBuffersLost/RealTimeBuffersLost最大均0；PID堆配置已关闭、cleanup_confirmed=true，helper已退出 |
| 游戏 | 采集后仍是原请求active/running、run8、完成6/50；profile SHA仍为`3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C` |

01:41收尾后，01:49:44仍处于`repeat.starting`，01:51:13已观察到run8 Running。源码`Application::watch_task_session`的starting包围`prepare_task`；轮间准备至少约8分钟，期间后台CPU计时持续增加。这里是一个实际性能调查线索，不是游戏停止或已确认的死锁。堆跟踪有开销，未做同负载无跟踪对照，**不能直接认定准备延迟的唯一原因**，也未擅自修改业务流程。

报告收口再次只读确认：run8已完成，批次7/50、run9 Running、仍为同一请求，采集helper与导出进程均已退出。这个后来的完成发生在采集关闭之后，不能回填为本次第二个堆快照窗口。

## 部分解析失败

完整导出入口仍拒绝不完整receipt。为了只读查看已保存的部分栈，另外生成`partial-analysis/partial-view.wpaProfile`，从冻结堆视图移除地址/heap handle列，保留Process/PID/Instance/Snap Time/Stack/Count/Size；源与派生哈希写在`scope.json`，没有更改原冻结profile或失败回执。分析限时120秒、输出目标128MiB，不提交新游戏任务。

这次CSV仍膨胀至**2224253602字节（2.071GiB）**，实际违反128MiB输出保护，命令以`TRACE_EXPORT_OUTPUT_LIMIT`失败。移除显示列并没有证明WPA按栈合并导出行；不能把派生profile的意图当成已经聚合的数据。没有遗留本次WPA导出进程，CSV保留为不完整失败产物，未加载整份到PowerShell内存，不据残缺行求总量或差分。

首个真实导出行已解析应用PDB，含`Application::run_status → NativeRunCoordinator::events → EventJournal::read → nlohmann::json`复制链，样本另存`first-row-sample.csv`供小范围复核。它只能证明采样时存在这类分配，不能证明这些块占增长的多少、长期滞留或解释历史45.11MiB。系统DLL/CRT仍标`PDB not found`，未伪造符号。`failure-summary.json`明确complete=false及UNRESOLVED。

输出保护读取FileInfo元数据未能及时阻止活动文件超限；元数据延迟、高吞吐与轮询滞后各自的贡献未实测归因。已将共用工具改成与ETL相同的FileStream.Length读取，并在子进程退出时再检查。对实际失败产物只读核对为超限、PowerShell语法检查0错误；**未再次运行导出或跟踪，因此不宣称新保护已在活动写入下验证或构成磁盘硬配额**。轮间准备、导出行膨胀及历史增长三个缺口均保留，本轮不继续追加采集。

## 解释边界

中途接入至首次join是部分窗口；两个join之间才是完整下一轮的同阶段差分。采样器也有目标进程内跟踪开销，不能把受跟踪私有提交变化当无开销基线；已启用前堆块、VirtualAlloc、缺符号模块与candidate119历史45.11MiB均未因此闭合。`RESOURCE_UNRESOLVED`继续保留。

## 已确认的调查线索

后续只读核对当前批次run2–8的worker_joined私有提交依次为68.89、71.77、74.47、77.68、81.25、118.76、93.01MiB，Session/OCR live均0、句柄362–366。run7处于堆跟踪中，run8收尾已在关闭后；这些样本不能当成统一无跟踪实验，也不能把118.76到93.01的回落全归因于某一个修复。运行中run9战斗采样约788.64MiB属于另一个阶段，不能与worker_joined相减证明每轮泄漏。

源码确认`next/native/app/application.cpp:1308`的run_status每次调用coordinator_->events()且复制到响应JSON；`next/native/storage/run_store.cpp:156`的EventJournal::read按after逐条复制事件，默认读取环中尚存的全部事件。真实部分分配栈与这条链吻合；监控轮询也增加此类分配。当前只确认分配热点及复制行为，尚无完整按栈净增量证明长期滞留，不能将它宣布为45.11MiB的根因。业务源码未改，下一步应先使低干扰边界采样与按栈聚合真实可用，而非重复累积总内存数字。

用户已授权将本轮工具、报告与当前事实提交并推送个人`fork`。本次提交不等于重新部署产品或追加采集，不纳入正式配置、ETL/CSV/运行日志及用户`.vscode`。
