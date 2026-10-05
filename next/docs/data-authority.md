# 当前数据权威

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
| `published/<request_id>/` | 本次编译封存的程序与资源身份；不可替代作者原文档。 | `publish_native` | 本次 `NativeRunDefinition` |
| `active-snapshots/` | 运行时物化快照，供原生会话读取；不是用户编辑入口。 | Bundle 发布/租约 | 识别与诊断 |
| `runs/<instance>/<run_id>/run.json`、`events.json`、`result.json` | 分别记录请求、活动事件和最终结果；只有 `result.json` 是持久化终态证据，`events.json` 不能反推业务完成。 | `RunStore`/`NativeRunCoordinator` | 工作台历史与诊断 |
| `runs/.../execution-events.jsonl` | 正式运行的逐项执行事件审计，先落盘再进入有界UI事件环；每轮64MiB上限，丢弃/失败计入`result.diagnostics.event_history`。终态仍只以`result.json`为权威，文件不提前写成功终态。与run/instance/seq关联，不是测试数据。 | `EventJournal`的唯一RunStore写入回调 | 只读计时分析器、故障排查 |
| `runs/.../action-timing.jsonl`、`diagnostics.jsonl` | 正式动作耗时/输入审计及按级别开关采集的诊断；上限分别64MiB/16MiB，终态冻结行数与失败计数。内存开启且info以上时，现有取帧回调每30秒记录进程/系统提交与可用物理内存；会话首帧、系统提交>=90%及失败收尾附带可读进程私有提交前8名（PID、创建时间、名称、私有/工作集字节），最多枚举4096项/200ms并记录不可读/截断/耗时，不采命令行、不额外截图、不终止进程。可读进程之和不是系统提交的完整分解。 | `RunStore` | 工作台诊断、只读分析器 |
| `runs/.../memory-lifecycle.json` | 终态之后的内存诊断，按运行定义释放、worker join、整批配置释放分别保存；最多三个具名快照，按run/instance关联，附进程范围Service/OCR/Session创建/销毁/live/ready标量。会话持有阶段的diagnostics另记租约ID/共享引用/原文件缓冲和程序参数容器估算。受内存开关与info级别控制，不回写终态、不作为业务成功条件；失败计数可由当前diagnostics读取。 | `NativeRunCoordinator`经`RunStore`；整批释放由唯一Application调用 | 只读分析器、资源归因 |
| `recent-frames/*.jpg`、`*.png` | 同一辅助滚动历史，不授权输入、不证明业务完成；周期JPEG至多每15秒一张，选人/输入前后/确认关键帧以无损PNG绕过周期限制并同帧去重。共用240张/128MiB上限，pending四张、in-flight一张，共享不可变BGR；`recent_frame.action`记录frame_id、阶段、是否入队，实际落盘仍须核对文件及失败/丢弃计数。 | `NativeRunCoordinator`提交，唯一`RunStore`线程写入 | 用户与诊断 |
| `runs/.../recognition-memory.log`及运行诊断文件 | 资源调查标量及必要故障证据；原memory/debug开关下附OCR owner ID及初始化/销毁begin/end私有内存/句柄配对，按run/generation关联，无新日志权威。配对差为进程观察差，非独占分配栈；普通采样限频，故障即时。只读归因工具的输出是独立诊断报告，不回写运行文件。 | Recognition/MemoryDiagnostics | 用户与诊断 |
| `legacy-import/` | 首次导入时对旧配置的私有副本，不回写源 `config.json`。 | 显式初次导入 | `ProfileStore` 初始化 |
| 源码 `resources/authoring/semantic-assets.json` | 素材语言、默认 condition 和显式 alternatives 的唯一人工配方源；流程只持有资源ID及可选method。 | 源码维护 | 作者目录、编译期展开、包同步 |
| 源码 `resources/recognition/ocr-models.json` | OCR来源、语言、SHA256、包路径的唯一锁；`.local/native-deps/ocr-zh-Hant`仅为可重建下载缓存。发布的`pack/model/ocr`为只读模型。 | 源码维护；prepare工具写缓存 | 构建、打包、发布器、识别Service |
| 源码 `packs/wvd/parameters/legacy-quests.json` | 内置任务目录及入口/路线声明；`_TIME_LEAP={target,chapter}`是任务内独立跳轮步骤，悬赏编译时优先于外部默认跳轮，不写入或覆盖profile。`GiantBounty`与旧第七区刷巨人是不同任务。候选`data/quest.json`是同源只读副本，不是用户运行数据。 | 源码维护；打包生成只读副本 | `WvdQuestCatalog`、`WvdTaskPlan`、任务编译器、工作台目录 |

候选目录的 `pack/`、`data/quest.json`、`web/` 是构建产物和只读输入，不能拿运行数据覆盖。历史 Maa 发布包及其诊断保留在旧目录或 Git 归档，只读展示，不作为新运行入口。当前结构以 `Application`、`ProfileStore`、`WorkflowRepository` 和 `RunStore` 的实际路径为准；旧阶段数据说明见 `archive/data-authority-maa-20260924.md`。

战斗调试文档仅在请求内存中生成，不写入`workflows/`。运行冻结指定已保存方案，不修改profile，也不接启动、重启或副本循环；调试回执、停止和诊断复用原协调器及RunStore。
