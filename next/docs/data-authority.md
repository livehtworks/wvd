# 数据与运行文件权威

本项目没有数据库。旧版 `config.json` 与 `mod` 继续由 Python 生产程序独占，
新版核心不覆盖或清理它们。M3 发现工具只读旧配置的设备定位字段，不进行完整配置导入。
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
| docs/migration/*.json | 固定 Git 基线的派生迁移盘点，不是运行调度源 | inventory 工具 | 只读工作台；所有迁移条目仍为 MAPPED_NOT_IMPLEMENTED |
| packs/wvd/image 与 manifest.json | 新版作者资源副本；固定 Git 源字节和 manifest hash 是来源权威 | prepare_m3.py | 独立 Bundle 装配；显式别名、动态引用去向；不写旧 resources |
| opencv.lock.json 与 .local/opencv.json | 前者是固定公开依赖锁，后者是私有安装路径 | prepare_m3.py，来自固定 MaaDeps 归档 | CMake 校验所有列出的头/库及与 Maa 相同的运行时 DLL |
| docs/migration/m3-implementation-map.json | 实现去向叠加，不改静态功能基线或宣布任务迁移完成 | m3_implementation_map.py | 审核报告；实现状态与质量状态分离 |
| .local 中显式指定的新 binding JSON | 私有设备线索和初始发现证据，不是场景许可 | validate_m3_device.py --discover | 本地 C++ 入口再次查管理器和控制者；不公开地址、路径、进程命令行 |
| .local/m3-gate-b.json | 本机离线结果与对应 EXE hash 的准入检查记录，不是游戏授权 | validate.py --m3 | 受限设备工具；重新验收先写 RUNNING，全部通过才写 PASS |
| .local/m3-device-* / m3-vision-* | 每次新建的可丢弃诊断证据；不是账号数据权威 | 独立验收工具、有限 C++ 会话 | 原图、帧身份、分段时间、结果、原生日志仅留本机；不并入资源包 |

## 一致性与边界

- SDK 对象、帧、识别缓存不是数据权威，不反写源图片或模型。
- 运行中的事实由 RunCoordinator 提供；历史目录没有已提交 result.json 时读为 Interrupted，禁止从未确认点击续跑。
- EventJournal 常规容量默认 256 条，普通事件可被淘汰并返回 resync_required。关键事件不能静默丢失；关键槽满则显式失败。
- 事件固定字段为 server_instance_id、run_id、session_generation、seq、monotonic_time（纳秒）、type、node_id、outcome、payload；不适用的节点/结果为 null，不从引擎成功推算业务成功。
- 另外保留一个终态槽。终态事件与结果一起提交，成功后才同时发布到内存；文件失败时序号和事件不前进。
- 单条普通事件载荷最多 64 KiB；终态事件只携带固定结构运行摘要。最终结果还含有界事件快照，不保存无限历史。
- 每个运行只提交一个结果；同目录临时写入、FlushFileBuffers、MoveFileEx 原子安装，失败保留临时证据和旧文件。
- definition_version=2 记录封存注册表、构建身份、动作/识别/恢复实现 revision 和显式参数。历史根 schema 保持 1；缺字段不补成可重放的新定义。
- 临时诊断 captures.json/live-status.json 是进行中的观测，不具备 result.json 的原子业务终态语义；原生未静止时不得根据部分诊断宣布完成。
- 当前没有日志自动清理、数据迁移或正式新版数据入口。长期保留期与配置编辑属于后续阶段，不能自动清理旧日志来补这些功能。
- 测试开始前由驱动创建新的唯一目录，所有截图/资源/结果均落在该目录，不使用运行目录默认配置。
