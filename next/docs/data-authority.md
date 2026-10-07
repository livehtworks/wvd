# 当前数据权威

收尾堆诊断的heap_summary_before/after记录HeapSummary可用性、完整性、堆数、失败数、allocated/committed/reserved及微秒耗时，仍由原RunStore按memory/info写入同一固定生命周期槽；不增加配置或另一运行所有者，不将堆内部提交当进程PrivateUsage。独立ProcDump诊断转储可能含用户配置/进程内容，仅保留在忽略的next/.local，禁止当普通报告提交或上传；无效小转储及超时原始日志保留负证据，不改变正式结果。

现有analyze_memory_owners可读取--run-directory与较早的--compare-run-directory，在权威数据外输出同进程/同维护边界的heap_comparison派生数字；输入只读，禁止在任一输入目录写报告。PID/创建/协调器/run顺序与采样完整性共同门控，比较成功不修改业务结果或宣称全部分配栈已归因。

独立堆布局取证由`capture_idle_heap.ps1`在权威data外写`cdb.log`、`regions.json`及`receipt.json`，使用CDB -pvr不暂停进程，只读同一进程的堆摘要、正式终态和batch_payloads_released；VirtualQueryEx另读提交区间元数据，不读内存内容、不更改profile、结果或业务状态。私有/映射/映像区间不是分配栈或对象所有权。可选分配大小表必须指定实际主堆地址且验证表存在，不能把摘要冒充大小表。缺符号/附着失败/期间发生运行切换必须标记不完整。工作线程收尾新增`heap_resources_optimized`诊断阶段，与原三个阶段并存，共四个固定槽；包含Windows空闲堆页回收前后私有提交、API错误和耗时，原worker_joined不被覆盖。该运行维护不受日志开关影响，样本落盘仍受memory/info门禁控制。

准备性能标量在原`diagnostics.jsonl`记录一次`preparation.completed`，由NativeRunCoordinator唯一写入，受performance开关及info级别控制；分段墙钟/线程CPU及文件/字节/节点数不进入program identity或profile。Application的既有operation只读摘要即时保留具名阶段开始/完成及current_phase，job结束保留该固定大小摘要，不复制到submission历史；开关关闭或warn以上不生成。隔离准备探针在独立临时目录逐阶段flush phase-events.jsonl，不写正式日志或新增数据权威。未启动验证和只读发布盘点只在权威data外写派生报告，不创建正式临时Schema，不赋予清理历史包权限。

项目没有数据库。旧版 `config.json`、`mod`、`logs` 和 `dist/wvd` 仍归旧 Python 程序及用户所有，新版不回写或清理。独立候选默认只写 `%LOCALAPPDATA%/WvdNext`，测试必须显式指定独立可丢弃的 `--data-root`。

| 文件/目录 | 权威与职责 | 写入者 | 读取者 |
| --- | --- | --- | --- |
| `profile.json` | 新版配置与战斗方案的唯一可写副本；revision 控制并发覆盖，保留旧字段 passthrough。 | `ProfileStore` 经 `Application` | 工作台、任务编译器 |
| `profile.json`中的`TASK_POINT_STRATEGY.special_combat.rules` | 怪物名称、方案引用、行动条头像ID及自定义PNG的唯一权威；随常用参数CAS保存，方案重命名同步重绑定。按列表顺序优先选择匹配的怪物。 | 工作台经原`ProfileStore`接口 | 战斗入口分类、配置引用保护、发布器 |
| `profile.json`中的`strategy_settings_version=2` | 统一六个友方目标和两种频次；默认值、有效值、隐藏任务覆盖和导入文档一并规范化。运行副本的移除不回写此配置。 | `ProfileStore`一次性迁移/创建/CAS保存 | 配置接口、战斗编译及结算 |
| `strategy-settings-v1-<revision>.json` | 迁移前profile原文的恢复备份，不是活动配置、不自动读取、不自动清理。包含用户配置，不能当可丢弃测试产物。 | `ProfileStore`迁移写前创建并核对已有备份 | 人工回滚 |
| `workflows/*.json` | 作者流程和公共定义唯一权威；引用删除保护、CAS revision。 | `WorkflowRepository` 经 `Application` | 工作台、闭包快照与原生编译器 |
| `asset-cache/` | 按候选 manifest 从只读资源包冻结的派生资产，非源素材权威。 | `Application` 的 manifest 冻结过程 | 发布器、识别服务 |
| `asset-cache/portraits-<digest>/` | 从profile嵌入PNG派生的只读、内容寻址图像包；与既有发布器一起冻结，不能反向回写profile或作为独立素材注册表。解码前检查PNG头和尺寸，发布核对哈希。 | `Application::portrait_bundle` | 原生发布器、运行租约 |
| `published/<request_id>/` | 本次编译封存的程序与资源身份；不可替代作者原文档。先在本次唯一staging复制/校验，释放临时锁后同盘rename，再按最终路径冻结；未启动取消只撤回本次目录，既有包不自动清理。 | `publish_native`，最终未启动回滚由Application门禁 | 本次 `NativeRunDefinition` |
| `active-snapshots/` | 运行时物化快照，供原生会话读取；不是用户编辑入口。 | Bundle 发布/租约 | 识别与诊断 |
| `runs/<instance>/<run_id>/run.json`、`events.json`、`result.json` | 分别记录请求、活动事件和最终结果；只有 `result.json` 是持久化终态证据，`events.json` 不能反推业务完成。 | `RunStore`/`NativeRunCoordinator` | 工作台历史与诊断 |
| `runs/.../execution-events.jsonl` | 正式运行的逐项执行事件审计，先落盘再进入有界UI事件环；每轮64MiB上限，丢弃/失败计入`result.diagnostics.event_history`。终态仍只以`result.json`为权威，文件不提前写成功终态。与run/instance/seq关联，不是测试数据。 | `EventJournal`的唯一RunStore写入回调 | 只读计时分析器、故障排查 |
| `runs/.../diagnostics/*.png`及`result.json`中的诊断索引 | 永久异常原帧，不属于recent-frames滚动历史。技能详情打开no_progress与随后防御分别落图、以operation_id关联，并核对原帧/generation/action_epoch/连接；缺图或跨连接显式不完整。失败共享32张/单帧8MiB额度，两张各占一次，context≤16KiB；operation-scoped去重不吞另一独立操作。图及配置技能不能证明释放成功或授权后续输入。 | `NativeRunCoordinator`沿现有RunStore唯一写入链 | 工作台诊断、只读故障复核 |
| `runs/.../action-timing.jsonl`、`diagnostics.jsonl` | 正式动作耗时/输入审计及按级别开关采集的诊断；上限分别64MiB/16MiB，终态冻结行数与失败计数。内存开启且info以上时，现有取帧回调每30秒记录进程/系统提交与可用物理内存；会话首帧、系统提交>=90%及失败收尾附带可读进程私有提交前8名（PID、创建时间、名称、私有/工作集字节），最多枚举4096项/200ms并记录不可读/截断/耗时，不采命令行、不额外截图、不终止进程。可读进程之和不是系统提交的完整分解。 | `RunStore` | 工作台诊断、只读分析器 |
| `runs/.../memory-lifecycle.json` | 终态之后的内存诊断，按运行定义释放、worker join、空闲堆资源回收、整批配置释放分别保存；最多四个具名快照，按run/instance关联，附进程范围Service/OCR/Session创建/销毁/live/ready标量。堆维护记录API结果、耗时和前后提交量，不覆盖维护前样本。会话持有阶段的diagnostics另记租约ID/共享引用/原文件缓冲和程序参数容器估算。受内存开关与info级别控制，不回写终态、不作为业务成功条件；失败计数可由当前diagnostics读取。 | `NativeRunCoordinator`经`RunStore`；整批释放由唯一Application调用 | 只读分析器、资源归因 |
| `recent-frames/*.jpg`、`*.png` | 同一辅助滚动历史，不授权输入、不证明业务完成；周期JPEG至多每15秒一张，选人/输入前后/确认关键帧以无损PNG绕过周期限制并同帧去重。共用240张/128MiB上限，pending四张、in-flight一张，共享不可变BGR；`recent_frame.action`记录frame_id、阶段、是否入队，实际落盘仍须核对文件及失败/丢弃计数。 | `NativeRunCoordinator`提交，唯一`RunStore`线程写入 | 用户与诊断 |
| `runs/.../recognition-memory.log`及运行诊断文件 | 资源调查标量及必要故障证据；原memory/debug开关下附OCR owner ID及初始化/销毁begin/end私有内存/句柄配对，按run/generation关联，无新日志权威。配对差为进程观察差，非独占分配栈；普通采样限频，故障即时。只读归因工具的输出是独立诊断报告，不回写运行文件。 | Recognition/MemoryDiagnostics | 用户与诊断 |
| 显式独立证据目录中的`allocations.etl`、`identity.json`、`checkpoints.json`、`collector-health.json`、`analysis/`及摘要 | 有限WPR取证及派生分配栈差分，不是正式业务数据。绑定PID/创建时间/instance/EXE/PDB，冻结采集与导出profile；PID快照只覆盖启用后栈。HeapAndVirtualAlloc模式为全系统VA后筛目标；用户明确指定的HeapSnapshots模式不采VA、不导出VA表。Collect要求空闲并显式提交有限任务；Monitor只读观察既有batch及真实worker_joined，不拥有或停止游戏，中途接入的首轮单列partial。最多2轮/20分钟/512MiB跟踪，导出另限120秒/128MiB；缺栈/丢失/容量/身份变化明确失败，归因未完成保留UNRESOLVED。证据目录必须在正式data之外，不写profile、workflows、RunStore表或日志结构。 | 显式调用`collect_memory_stacks.ps1`，WPR保存；`export_memory_stacks.ps1`只读派生 | 用户、WPA及故障报告 |
| `legacy-import/` | 首次导入时对旧配置的私有副本，不回写源 `config.json`。 | 显式初次导入 | `ProfileStore` 初始化 |
| 源码 `resources/authoring/semantic-assets.json` | 素材语言、默认 condition 和显式 alternatives 的唯一人工配方源；流程只持有资源ID及可选method。 | 源码维护 | 作者目录、编译期展开、包同步 |
| 源码 `resources/recognition/ocr-models.json` | OCR来源、语言、SHA256、包路径的唯一锁；`.local/native-deps/ocr-zh-Hant`仅为可重建下载缓存。发布的`pack/model/ocr`为只读模型。 | 源码维护；prepare工具写缓存 | 构建、打包、发布器、识别Service |
| 源码 `packs/wvd/parameters/legacy-quests.json` | 内置任务目录及入口/路线声明；`_TIME_LEAP={target,chapter}`是任务内独立跳轮步骤，悬赏编译时优先于外部默认跳轮，不写入或覆盖profile。`GiantBounty`与旧第七区刷巨人是不同任务。候选`data/quest.json`是同源只读副本，不是用户运行数据。 | 源码维护；打包生成只读副本 | `WvdQuestCatalog`、`WvdTaskPlan`、任务编译器、工作台目录 |

候选目录的 `pack/`、`data/quest.json`、`web/` 是构建产物和只读输入，不能拿运行数据覆盖。历史 Maa 发布包及其诊断保留在旧目录或 Git 归档，只读展示，不作为新运行入口。当前结构以 `Application`、`ProfileStore`、`WorkflowRepository` 和 `RunStore` 的实际路径为准；旧阶段数据说明见 `archive/data-authority-maa-20260924.md`。

战斗调试文档仅在请求内存中生成，不写入`workflows/`。运行冻结指定已保存方案，不修改profile，也不接启动、重启或副本循环；调试回执、停止和诊断复用原协调器及RunStore。

2026-10-07的仅堆窗口`memory-monitor-heap-two-cbdf2cc6-4ba7-43d7-8013-8088c8d32eab`已关闭，receipt因缺第二个join保持不完整。其`partial-analysis/`是另外标注范围的只读探索，未改原冻结profile、ETL或receipt；CSV实际2.071GiB超出128MiB导出目标，failure-summary和首行样本保留，不能作为完整差分或内存归因权威。工具保护的改动不回写这个失败结果，也不代表已证明硬容量约束。

当前堆派生分析由`export_memory_stacks.ps1`唯一包装调用独立x64 TraceProcessor，写新的分析子目录、计划及回执，不再经过WPA明细CSV。`heap-outstanding-by-stack.csv`/`heap-delta-by-stack.csv`是叶级完整数值，`stack-dictionary.jsonl`是去重地址/展示栈，`snapshot-map.json`按真实时间绑定phase，`analysis-receipt.json`区分capture/analysis/comparison/attribution。输入ETL/旧receipt/profile只读；部分分析与旧边界不赋予业务完成或输入权限。SDK内部缓冲由独立进程Job限额承接，VirtualAlloc不在该堆分析器解释范围。
