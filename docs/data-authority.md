# 持久数据职责

本页是当前数据职责与保护边界的统一入口；[Next详细说明](../next/docs/data-authority.md)补充诊断字段和消费关系，不另设一套生命周期规则。现行行为以 `179cfd851877cba2f2df401d86a905ff9ae088f6` 对应代码为核对基线；本地现场仅引用2026-10-26盘点，不表示本次重新实测。

不存在正式关系数据库；以下文件/目录承担数据权威。除标为源码、候选或独立证据的项目外，路径均相对于启动时独占的data-root。测试必须使用全新 `next/.local/` 子目录，不能默认连接正式目录。

| 对象 | 职责与关键数据 | 生产者/消费者 | 属性与关联 |
| --- | --- | --- | --- |
| profile.json | 设备、任务、战斗、日志配置及revision | ProfileStore；工作台分区CAS保存/任务准备 | 用户权威；提交前核revision，不覆盖用户编辑 |
| workflows/*.json | 用户流程、内置拷贝和本地revision；execution.checks.required声明业务职责，inherit声明允许继承的事件 | WorkflowRepository；编辑/编译/执行 | 用户权威，正式保存版本优先于包内默认；职责不是由继承白名单反推，仍以原revision CAS写入 |
| workflows/.builtin | 内置来源、本地版本、last_sync回执 | WorkflowRepository | 与对应文档成单事务；GET不修补 |
| workflows/.builtin-backups | 同步前准确文档及元数据 | 内置同步事务；人工恢复 | 不可变用户恢复依据，不自动清理 |
| workflows/.builtin-transactions | pending意图/completed历史、前后版本/哈希 | 仓库写入/构造时恢复 | 有界核验完成或拒绝；禁止覆盖未知本地修改 |
| requests/<request-id-sha256>.json | 原始请求意图、原run、接受/终态回执 | SubmissionStore/Application；重试查询 | 持久幂等事实，1MiB/条、256MiB总量、65,536条；替换不增加条数。内部SHA/UUID临时文件共同计量；未完成/未知或超额时只读，旧ID仍按需核验读取，不重执行未知意图 |
| runs/<instance>/<run>/ | run定义、result、事件、输入、诊断/计时/内存边界 | Coordinator/Session/RunStore；API/诊断工具 | 运行证据；instance/run/gen绑定，result.json才是根终态 |
| published/<request>/ | 冻结IR、manifest及资源引用 | NativePublisher；Coordinator/Session | 派生不可变发布，唯一所有权token随组装传递；明确接管前失败只按身份撤回本次目录，接管后即使快速完成也保留；活跃租约禁止修改/删除 |
| published/asset-objects/<hash>/ | 从锁定原始字节复制的一份模型 | NativePublisher；同盘发布硬链接 | 派生内容缓存；仅系统真实STATUS_TOO_MANY_LINKS时从同一冻结租约流式复制；不得硬链接用户可变原件，无自动历史删除 |
| published、asset-objects、asset-cache中的.cleanup-pending-*及本次staging | 改名前准备意图/源目标身份，或清理故障/残留现场；双错误另由原异常及Application状态报告 | OwnedBundleDirectory/Application；准备准入 | 派生事务留证；接管或完整撤回后才移除本次索引。遗留意图不是当前接管结论，阻止新准备，重启需核持久run/request引用，不猜所有权或批量清理 |
| recent-frames/ | 全局普通JPG/动作PNG及内部未提交tmp | RunStore原有recent worker；诊断查询 | 可选有界诊断；240张/128MiB、单张8MiB、队列4张。先核身份/计量/回收预留再写；失败阻断新增，重建不清零，显式重新核验后才可恢复 |
| asset-cache、active-snapshots、probe-snapshots | 冻结资源/自定义怪物、活动租约 | Application/BundleLease；编译/识别 | 派生缓存/快照，非配置权威；清理需核所有者 |
| 源码next/resources/authoring、next/resources/recognition/ocr-models.json | 人工配方、公共流程及模型来源/语言/哈希锁 | 源码维护；显式资源生成及编译 | 作者源/模型锁权威；不在stage中隐式改写 |
| 源码next/packs/wvd/parameters/legacy-quests.json | 内置任务目录、入口与路线声明 | 源码维护；任务编译及工作台 | 任务目录源；候选data/quest.json是同源只读副本，不是用户运行数据 |
| 源码next/packs/wvd及候选pack_root | manifest管理的打包输入与已冻结只读运行输入；已有作者源的生成副本不替代其源文件 | 显式资源生成；package_functional/编译及语言闭包 | 按成员区分作者输入和生成副本，生成与发布身份分别核验；stage只校验并输出候选，不向作者副本回写 |
| 候选旁的.<current>.transactions/；资源生成的.local/resource-generation/<pack>/ | writer.lock、transaction.json、prepared/previous身份和阶段 | resource_generation受限目录事务；package_functional及恢复 | 工具派生事务，复制前落意图；持锁恢复上一笔，历史及失败材料计量，不自动删除未知历史 |
| 候选目录/service-launch.json | 已核验服务的data-root、端口与启动参数 | manage_service原入口；后续服务控制 | 新进程及owner核验成功后原子提交；失败保留旧字节，提交失败另报真实新进程，不自动杀掉或重启 |
| 独立测试/证据目录 | 本轮回执、失败证据、必要复现材料及可重建夹具 | 原合同/诊断工具；只读审阅 | 非正式业务权威；测试用新根，结束后按认领清单区分必要证据和大夹具，未知材料保留待核 |
| 旧config.json、mod、dist | 旧配置/用户扩展/历史产物 | 明确入口只读导入 | 历史/用户资产，不原生双读双写，不覆盖、不删 |

本次文档收束没有迁移或改写正式数据。新requests历史只由原生Application写入，不复制猜测旧内存幂等表；进程退出留下preparing意图表示已接受但结果未知，不能自动重执行。达到总额度停止接受新意图，既有回执仍可读取。移出活动目录不能抹掉旧ID的幂等事实、业务未知或恢复引用。

2026-10-27候选增加显式历史维护入口，正式尚未启用。`data-root/.publication-maintenance.json`为持久门禁，`.publication-maintenance-journal.json`为其唯一事务登记（最多2048笔/16MiB）；维护器持有同一`service.lock`，记录源目录身份、原始控制文件/历史证据SHA、共享集合及阶段，不成为请求或业务结果权威。未知、缺件、损坏或未终结事务阻止正常启动与新准备，须显式finish/rollback，不自动解锁或回退。原树保存在同卷`trash/wvd-publication-history/<data-root身份>/<事务>/`，属于受保护恢复材料。requests/runs不写入、不移动；repeat必须显式提供根请求与轮次，并核验完整成功前缀，停止半轮原样保留。

现行准入与保护：每轮准入至少2GiB可用，整批引用字节16GiB上限，必要输入/终态与关键失败图预留；普通采样有界降采样并报告。这些是局部准入限制，不是磁盘物理预分配、全工作区预算或已实施的历史退出策略；不保证外部程序抢占空间后仍能写入。低空间保留旧权威并停止新轮，历史不擅删。

上表保留当前正式部署的数据职责；新候选源码已实现`published/.asset-sets/<digest>/`单套共享资源及请求独立program/identity，通过组合租约统一读取。新marker无效时拒绝读取，旧完整布局仍按原严格校验读取，requests/runs原权威不变；候选实现及离线验收不代表已经切换正式服务或迁移历史。

有界保留、历史引用退出、测试产物收尾与可恢复迁移的实际进度见 [收束计划](work-packages/consolidation-20261026.md) 及 [任务登记](work-packages/consolidation-20261026-tasks.json)。本页不规定新的保留天数、回滚份数或自动删除阈值，也不授予生产清理权限。各项须承接活动租约、未结副作用、请求幂等、历史查询与恢复能力，沿现有Application/Coordinator/RunStore/Publisher职责落实；不得新增外部清理守护器或第二份权威。

整理前保护当前/已核回滚候选、交付和构建回执、启动绑定及准确匹配的EXE/PDB。符号配对须核GUID/Age并记录双方SHA，不能仅凭相邻目录或同名判断；现有打包产物清单不自动包含PDB。候选、符号、构建来源及业务数据分别记录，不通过搬目录或改标记伪造承接。
