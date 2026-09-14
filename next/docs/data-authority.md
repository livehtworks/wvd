# 数据与运行文件权威

本项目没有数据库。旧版 `config.json` 与 `mod` 继续由 Python 生产程序独占，
新版核心不覆盖或清理它们。M3 发现工具只读旧配置的设备定位字段；M4 独立导入器只在新目录创建副本，
不接入正式配置或业务执行，真实设备发现当前阻断。
下表记录当前已落地的数据，不把未来配置目录当作现有结构。

## 当前有效数据

| 数据或文件 | 属性与权威 | 生产者 | 消费者与关联 |
| --- | --- | --- | --- |
| 调用者显式提供的 data_root/instance/run/run.json | 新版运行元数据；只创建，不接管同名目录 | RunStore，来自冻结 RunDefinition | 离线历史读取；关联 instance、run_id、request_id、权限、资源清单/hash、预算 |
| 同目录 events.json | 活动诊断快照，不是最终完成权威 | EventJournal 经 RunStore 原子替换 | 诊断查看；可能停在终态提交之前，不能据此覆盖 result.json |
| 同目录 result.json | 一次性应用终态权威，包括引擎状态、根终点证据、输入计数及完整有界终态事件快照 | RunCoordinator 确认真正静止后，RunStore 原子提交 | 历史读取、验收；与同目录 run.json 对应，不允许重复提交 |
| 同目录 *.tmp | 提交失败或进程中断的诊断证据，不是正式结果 | Windows 原子写入过程 | 只读排查，不自动续跑或覆盖已提交数据 |
| next/.local/maafw.json 与 SDK/模型缓存 | 本机依赖准备记录及派生缓存；固定锁文件/hash 是版本权威 | prepare_maafw.py | CMake 与离线测试；含私人路径，不提交 |
| next/.local/m2-runs/各独立测试目录 | 本轮可丢弃测试数据，绝非用户配置或业务记录 | 每次 unittest 创建新目录及合成资源 | 测试驱动、真实 Maa SDK、审核；每个用例的 run-data 是专用隔离写入根 |
| docs/migration/feature_inventory.json | 固定 Git 基线的静态盘点，不是运行调度源 | inventory 工具 | 只读工作台；基线状态不随实现改写，当前实现看 M3/M4 叠加台账 |
| packs/wvd/image 与 manifest.json | 新版作者资源副本；固定 Git 源字节和 manifest hash 是来源权威 | prepare_m3.py | 独立 Bundle 装配；显式别名、动态引用去向；不写旧 resources |
| opencv.lock.json 与 .local/opencv.json | 前者是固定公开依赖锁，后者是私有安装路径 | prepare_m3.py，来自固定 MaaDeps 归档 | CMake 校验所有列出的头/库及与 Maa 相同的运行时 DLL |
| docs/migration/m3-implementation-map.json | 实现去向叠加，不改静态功能基线或宣布任务迁移完成 | m3_implementation_map.py | 审核报告；实现状态与质量状态分离 |
| .local 中显式指定的新 binding JSON | 私有设备线索和初始发现证据，不是场景许可 | validate_m3_device.py --discover | 本地 C++ 入口再次查管理器和控制者；不公开地址、路径、进程命令行 |
| .local/m3-gate-b.json | 本机离线结果与对应 EXE hash 的准入检查记录，不是游戏授权 | validate.py --m3 | 受限设备工具；重新验收先写 RUNNING，全部通过才写 PASS |
| .local/m3-device-* / m3-vision-* | 每次新建的可丢弃诊断证据；不是账号数据权威 | 独立验收工具、有限 C++ 会话 | 原图、帧身份、分段时间、结果、原生日志仅留本机；不并入资源包 |
| 私有 snapshot_parent/unique_id/ | 活动 SDK 资源快照及结束后的诊断副本；源清单/hash 绑定 | materialize_bundle 从持有的源句柄复制 | MaaGateway 与 AssetResolver 只读消费；BundleLease 覆盖原生生命周期，不与源共享文件对象 |
| snapshot_parent/revisions/identity_hash.json | 作者路径/revision 对应清单的一次性索引，不是业务数据库 | materialize_bundle 原子首次创建 | 同一身份不同清单拒绝；不自动覆盖或清理 |
| publish_workflow 显式新目录中的 image/、pipeline/workflow.json | 新版有限子流程的作者资源派生包，不是旧资源或完整任务权威 | 编译器校验后从持锁源复制；只创建新目录，不覆盖既存目录 | SessionDefinition 绑定文件 hash、图、所需权限、源 revision、别名及注册表构建身份；运行再物化为活动快照 |
| 同一发布目录 parameters/image-sources.json（显式提供 mod 时） | 本次派生图片的来源记录，不含私人绝对路径 | publish_workflow 从基线与 mod 的显式 manifest 持锁复制，记录逻辑名、所选相对路径、来源和 hash | 随 Session Bundle 封存供诊断；运行只读派生包，不再扫描或读取用户 mod |
| packs/wvd/parameters/legacy-config-fields.json、legacy-quests.json | 固定旧 Git 的配置描述与任务字节副本，非用户配置 | prepare_m4.py | LegacyConfigImporter、WvdQuestCatalog；只为离线数据检查使用 |
| schemas/wvd-profile.schema.json | 新版副本的公开结构契约 | prepare_m4.py | 审核与工具；原生解析器同时执行类型及嵌套业务校验，不代表已接入运行配置 |
| 显式新目的目录/legacy-config.json | 旧配置的私有只读复制结果，含用户数据，不可公开 | LegacyConfigImporter::import_copy | 从副本解析并验证源 hash；没有向旧配置写回的路径 |
| 显式新目录中的 profile JSON / 同名 .lock | 新版离线配置副本及写者互斥锁；完整字段、来源和 passthrough 是副本权威 | ProfileStore | 新副本读写及 CAS；Run 只接收显式冻结的 values，不监视或重读文件；没有 GUI/生产写入者 |
| run.json definition.state_factory、continuation_units | 新版有限运行的冻结定义，含状态工厂身份、配置值、段顺序和预算 | RunCoordinator / RunStore | 非捕获工厂创建本 Run 独占状态；修改调用者原对象不改变已启动 Run |
| 恢复 Session 的 lifecycle 计划及 lifecycle.* 事件 | 本 Run 授权目标、有限操作和实际调用证据；不是任意 shell 或重放许可 | WVD 纯策略生成冻结计划，ExecutionSession 验证并调用离线 LifecyclePort | 每步核对身份/时效/连接代次和后置状态；停止仍由原 Run 持有资源 |
| snapshot.business.lifecycle_recovery_active/sequence | 本 Run 恢复请求边界，不是跨进程检查点 | WvdRunState 在 LifecycleRecovery 边界和新帧 game_restarted 确认时更新 | 区分同次升级与启动成功后的新故障；不以曾有 lifecycle 计划推断当前仍在恢复 |
| result.json snapshot.business、sessions[].business/checkpoint | 静止边界的业务摘要及根检查点证据，不是崩溃后自动续跑许可 | RunCoordinator 在 Session join 后拷贝 | 历史审查与离线验收；completed_business_units 单独计数，不把一个段当整条任务 |
| WvdRunState 确认签名、business.confirmed 事件、snapshot.business.last_confirmation | 本 Run 的操作去重与确认诊断；不是跨进程恢复数据库 | WvdConfirm 在实际新帧识别后生成；状态最多保存 4096 个签名，EventJournal 有界 | 任务步/宝箱计数与终态审查；摘要不等于持久化 profile 回执，不允许据此启动实机重放 |
| snapshot.business.inn_rest_completed/inn_rests/supply_cycle、last_bag_clear | 本 Run 已确认住宿与队伍周期；不是装备或游戏资产权威 | 新帧确认后由 WvdRunState 更新，入本开始新补给周期 | 回城补给和旅店子图避免已确认住宿重付；跨普通段/恢复代次保留，不从磁盘历史自动重建许可 |
| WvdRunState prepared 选择及 business.combat 事件 | 本代次未完成技能意图和已确认消费诊断；不是恢复许可 | WvdCombat 在同帧头像匹配后选择；新帧后置确认后消费 | 技能图读取只读摘要并重新识别原角色；不保存旧帧/坐标，段边界清除未完成选择 |
| docs/migration/m4-implementation-map.json、m4-task-status.json | 当前实现叠加和完整任务分母；不是执行计划 | m4_inventory.py 对照固定索引与实际数据测试结果 | 审核；DATA_BOUND_ONLY 不能解释为任务已运行 |
| .local/m3fix-m4-*、m3-fixes-*、m4-data-* | 本轮隔离配置、合成图、失败复现与测试结果 | 工作包执行脚本、原生检查入口 | 私有审核证据；公开包仅收脱敏报告/索引，不包含配置原文或真实图片 |

## 一致性与边界

- SDK 对象、帧、识别缓存不是数据权威，不反写源图片或模型。
- 运行中的事实由 RunCoordinator 提供；历史目录没有已提交 result.json 时读为 Interrupted，禁止从未确认点击续跑。
- EventJournal 常规容量默认 256 条，普通事件可被淘汰并返回 resync_required。关键事件不能静默丢失；关键槽满则显式失败。
- 事件固定字段为 server_instance_id、run_id、session_generation、seq、monotonic_time（纳秒）、type、node_id、outcome、payload；不适用的节点/结果为 null，不从引擎成功推算业务成功。
- 另外保留一个终态槽。终态事件与结果一起提交，成功后才同时发布到内存；文件失败时序号和事件不前进。
- 单条普通事件载荷最多 64 KiB；终态事件只携带固定结构运行摘要。最终结果还含有界事件快照，不保存无限历史。
- 每个运行只提交一个结果；同目录临时写入、FlushFileBuffers、MoveFileEx 原子安装，失败保留临时证据和旧文件。
- definition_version=3 在封存注册表、构建身份和行为参数之外记录状态工厂、正常续段及 max_business_units。历史 version=2 不改写，根 schema 保持 1；缺字段不补成可重放的新定义。
- 临时诊断 captures.json/live-status.json 是进行中的观测，不具备 result.json 的原子业务终态语义；原生未静止时不得根据部分诊断宣布完成。
- 当前只有显式离线副本导入，没有自动数据迁移、日志自动清理或正式新版数据入口。长期保留期与配置编辑属于后续阶段，不能自动清理旧日志来补这些功能。
- 测试开始前由驱动创建新的唯一目录，所有截图/资源/结果均落在该目录，不使用运行目录默认配置。
