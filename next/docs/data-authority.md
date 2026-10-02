# 当前数据权威

项目没有数据库。旧版 `config.json`、`mod`、`logs` 和 `dist/wvd` 仍归旧 Python 程序及用户所有，新版不回写或清理。独立候选默认只写 `%LOCALAPPDATA%/WvdNext`，测试必须显式指定独立可丢弃的 `--data-root`。

| 文件/目录 | 权威与职责 | 写入者 | 读取者 |
| --- | --- | --- | --- |
| `profile.json` | 新版配置与战斗方案的唯一可写副本；revision 控制并发覆盖，保留旧字段 passthrough。 | `ProfileStore` 经 `Application` | 工作台、任务编译器 |
| `profile.json`中的`strategy_settings_version=2` | 统一六个友方目标和两种频次；默认值、有效值、隐藏任务覆盖和导入文档一并规范化。运行副本的移除不回写此配置。 | `ProfileStore`一次性迁移/创建/CAS保存 | 配置接口、战斗编译及结算 |
| `strategy-settings-v1-<revision>.json` | 迁移前profile原文的恢复备份，不是活动配置、不自动读取、不自动清理。包含用户配置，不能当可丢弃测试产物。 | `ProfileStore`迁移写前创建并核对已有备份 | 人工回滚 |
| `workflows/*.json` | 作者流程和公共定义唯一权威；引用删除保护、CAS revision。 | `WorkflowRepository` 经 `Application` | 工作台、闭包快照与原生编译器 |
| `asset-cache/` | 按候选 manifest 从只读资源包冻结的派生资产，非源素材权威。 | `Application` 的 manifest 冻结过程 | 发布器、识别服务 |
| `published/<request_id>/` | 本次编译封存的程序与资源身份；不可替代作者原文档。 | `publish_native` | 本次 `NativeRunDefinition` |
| `active-snapshots/` | 运行时物化快照，供原生会话读取；不是用户编辑入口。 | Bundle 发布/租约 | 识别与诊断 |
| `runs/<instance>/<run_id>/run.json`、`events.json`、`result.json` | 分别记录请求、活动事件和最终结果；只有 `result.json` 是持久化终态证据，`events.json` 不能反推业务完成。 | `RunStore`/`NativeRunCoordinator` | 工作台历史与诊断 |
| `runs/.../execution-events.jsonl` | 正式运行的逐项执行事件审计，先落盘再进入有界UI事件环；每轮64MiB上限，丢弃/失败计入`result.diagnostics.event_history`。终态仍只以`result.json`为权威，文件不提前写成功终态。与run/instance/seq关联，不是测试数据。 | `EventJournal`的唯一RunStore写入回调 | 只读计时分析器、故障排查 |
| `runs/.../action-timing.jsonl`、`diagnostics.jsonl` | 正式动作耗时/输入审计及按级别开关采集的诊断；上限分别64MiB/16MiB，终态冻结行数与失败计数。 | `RunStore` | 工作台诊断、只读分析器 |
| `runs/.../memory-lifecycle.json` | 终态之后的内存诊断，按运行定义释放、worker join、整批配置释放分别保存；最多三个具名标量快照，按run/instance关联。受内存开关与info级别控制，不回写终态、不作为业务成功条件；失败计数可由当前diagnostics读取。 | `NativeRunCoordinator`经`RunStore`；整批释放由唯一Application调用 | 只读分析器、资源归因 |
| `recent-frames/*.jpg` | 辅助滚动现场历史，不授权输入、不证明业务完成；至多每15秒提交一帧，沿用240张/128MiB保留上限。pending/in-flight各一张，共享不可变BGR。 | `RunStore`自有线程 | 用户与诊断 |
| `runs/.../recognition-memory.log`及运行诊断文件 | 资源调查标量及必要故障证据，不是资源稳定或泄漏归因结论；普通采样限频，故障即时。 | Recognition/MemoryDiagnostics | 用户与诊断 |
| `legacy-import/` | 首次导入时对旧配置的私有副本，不回写源 `config.json`。 | 显式初次导入 | `ProfileStore` 初始化 |
| 源码 `resources/authoring/semantic-assets.json` | 素材语言、默认 condition 和显式 alternatives 的唯一人工配方源；流程只持有资源ID及可选method。 | 源码维护 | 作者目录、编译期展开、包同步 |
| 源码 `resources/recognition/ocr-models.json` | OCR来源、语言、SHA256、包路径的唯一锁；`.local/native-deps/ocr-zh-Hant`仅为可重建下载缓存。发布的`pack/model/ocr`为只读模型。 | 源码维护；prepare工具写缓存 | 构建、打包、发布器、识别Service |

候选目录的 `pack/`、`data/quest.json`、`web/` 是构建产物和只读输入，不能拿运行数据覆盖。历史 Maa 发布包及其诊断保留在旧目录或 Git 归档，只读展示，不作为新运行入口。当前结构以 `Application`、`ProfileStore`、`WorkflowRepository` 和 `RunStore` 的实际路径为准；旧阶段数据说明见 `archive/data-authority-maa-20260924.md`。
