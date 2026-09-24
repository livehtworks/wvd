# 当前数据权威

项目没有数据库。旧版 `config.json`、`mod`、`logs` 和 `dist/wvd` 仍归旧 Python 程序及用户所有，新版不回写或清理。独立候选默认只写 `%LOCALAPPDATA%/WvdNext`，测试必须显式指定独立可丢弃的 `--data-root`。

| 文件/目录 | 权威与职责 | 写入者 | 读取者 |
| --- | --- | --- | --- |
| `profile.json` | 新版配置与战斗方案的唯一可写副本；revision 控制并发覆盖，保留旧字段 passthrough。 | `ProfileStore` 经 `Application` | 工作台、任务编译器 |
| `workflows/*.json` | 作者流程和公共定义唯一权威；引用删除保护、CAS revision。 | `WorkflowRepository` 经 `Application` | 工作台、闭包快照与原生编译器 |
| `asset-cache/` | 按候选 manifest 从只读资源包冻结的派生资产，非源素材权威。 | `Application` 的 manifest 冻结过程 | 发布器、识别服务 |
| `published/<request_id>/` | 本次编译封存的程序与资源身份；不可替代作者原文档。 | `publish_native` | 本次 `NativeRunDefinition` |
| `active-snapshots/` | 运行时物化快照，供原生会话读取；不是用户编辑入口。 | Bundle 发布/租约 | 识别与诊断 |
| `runs/<instance>/<run_id>/run.json`、`events.json`、`result.json` | 分别记录请求、活动事件和最终结果；只有 `result.json` 是持久化终态证据，`events.json` 不能反推业务完成。 | `RunStore`/`NativeRunCoordinator` | 工作台历史与诊断 |
| `legacy-import/` | 首次导入时对旧配置的私有副本，不回写源 `config.json`。 | 显式初次导入 | `ProfileStore` 初始化 |

候选目录的 `pack/`、`data/quest.json`、`web/` 是构建产物和只读输入，不能拿运行数据覆盖。历史 Maa 发布包及其诊断保留在旧目录或 Git 归档，只读展示，不作为新运行入口。当前结构以 `Application`、`ProfileStore`、`WorkflowRepository` 和 `RunStore` 的实际路径为准；旧阶段数据说明见 `archive/data-authority-maa-20260924.md`。
