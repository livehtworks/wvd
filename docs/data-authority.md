# 持久数据职责

不存在正式关系数据库；以下文件/目录承担数据权威。路径均相对于启动时独占的data-root；测试必须使用全新 `next/.local/` 子目录，不能默认连接正式目录。

| 对象 | 职责与关键数据 | 生产者/消费者 | 属性与关联 |
| --- | --- | --- | --- |
| profile.json | 设备、任务、战斗、日志配置及revision | ProfileStore；工作台分区CAS保存/任务准备 | 用户权威；提交前核revision，不覆盖用户编辑 |
| workflows/*.json | 用户流程、内置拷贝和本地revision | WorkflowRepository；编辑/编译/执行 | 用户权威，正式保存版本优先于包内默认 |
| workflows/.builtin | 内置来源、本地版本、last_sync回执 | WorkflowRepository | 与对应文档成单事务；GET不修补 |
| workflows/.builtin-backups | 同步前准确文档及元数据 | 内置同步事务；人工恢复 | 不可变用户恢复依据，不自动清理 |
| workflows/.builtin-transactions | pending意图/completed历史、前后版本/哈希 | 仓库写入/构造时恢复 | 有界核验完成或拒绝；禁止覆盖未知本地修改 |
| requests/<request-id-sha256>.json | 原始请求意图、原run、接受/终态回执 | SubmissionStore/Application；重试查询 | 持久幂等事实，1MiB/条、256MiB总量；不因缓存淘汰遗忘，不重执行已接受意图 |
| runs/<instance>/<run>/ | run定义、result、事件、输入、诊断/计时/内存边界 | Coordinator/Session/RunStore；API/诊断工具 | 运行证据；instance/run/gen绑定，result.json才是根终态 |
| published/<request>/ | 冻结IR、manifest及资源引用 | NativePublisher；Session | 派生不可变发布，失败只撤回本次未启动目录；活跃租约禁止修改/删除 |
| published/asset-objects/<hash>/ | 从锁定原始字节复制的一份模型 | NativePublisher；同盘发布硬链接 | 派生内容缓存；不得硬链接用户可变原件，无自动历史删除 |
| asset-cache、active-snapshots、probe-snapshots | 冻结资源/自定义怪物、活动租约 | Application/BundleLease；编译/识别 | 派生缓存/快照，非配置权威；清理需核所有者 |
| pack_root、resources/authoring | 默认流程、模板、模型锁、作者定义 | 资源生成；编译/语言闭包 | 源码权威，不在stage中隐式改写；生成事务保留旧输出 |
| 旧config.json、mod、dist | 旧配置/用户扩展/历史产物 | 明确入口只读导入 | 历史/用户资产，不原生双读双写，不覆盖、不删 |

本轮没有迁移或改写正式数据。新requests历史只由原生Application写入，不复制猜测旧内存幂等表；进程退出留下preparing意图表示已接受但结果未知，不能自动重执行。达到总额度停止接受新意图，既有回执仍可读取；归档/清理需独立人工计划与授权。

保留策略：每轮准入至少2GiB可用，整批引用字节16GiB上限，必要输入/终态与关键失败图预留；普通采样有界降采样并报告。此为准入政策，不是磁盘物理预分配，不保证外部程序抢占空间后仍能写入。历史不擅删；低空间保留旧权威并停止新轮，用户另行决定有备份的归档。
