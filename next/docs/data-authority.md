# 数据与运行文件权威

本项目没有数据库。旧版 `config.json` 与 `mod` 继续由 Python 生产程序独占，
新版核心不覆盖或清理它们。M3 发现工具只读旧配置的设备定位字段；M4 独立导入器只在新目录创建副本，
不接入正式配置或业务执行，真实设备发现当前阻断。
下表记录当前已落地的数据，不把未来配置目录当作现有结构。

## 当前有效数据

指定公会任务访问的运行事实由 `quests::FeaturedVisit` 经 WvdRunState 独占：
`featured_visit` 保存 active/pending、访问序号、完成访问数和已确认选择次数。
选择次数只在选择后的城内新帧确认后增加，不代表任务奖励或物品数量；
已领取的牛洞访问只增加访问数。恢复保留未决意图，不写旧配置或任务资源。

忍洞运行事实由 `quests::GoldenChestCycle` 经同一WvdRunState独占，摘要为
`golden_chest`。phase、leap_pending和unit_matches约束两段正常续接；
completed_cycles只在六点及退出已确认后增加，不把任务点数或开始计数作为完成权威。

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
| 显式新目录中的 profile JSON / 同名 .lock | 新版离线配置副本及写者互斥锁；完整字段、来源和 passthrough 是副本权威 | ProfileStore | Run 接收显式冻结 values；善恶值另需显式路径/revision 绑定，仅确认后 CAS，不监视文件、不回写旧 config；没有 GUI/生产写入者 |
| profile.last_business_update（schema1 可选）、snapshot.business.karma_effect | 最新一次善恶值确认与保存回执；历史完整回执以各 RunStore 结果为准 | WvdConfirm 生成带操作编号、原值/新值、代次/帧的事实；KarmaCommitPort 的存储适配原子保存新 profile | 旧 schema1 缺此字段表示未记录，不补造已确认；保存失败保留 Run 内 Failed 和已确认新值，不重发输入、不自动恢复 |
| run.json definition.state_factory、continuation_units | 新版有限运行的冻结定义，含状态工厂身份、配置值、段顺序和预算 | RunCoordinator / RunStore | 非捕获工厂创建本 Run 独占状态；修改调用者原对象不改变已启动 Run |
| 恢复 Session 的 lifecycle 计划及 lifecycle.* 事件 | 本 Run 授权目标、有限操作和实际调用证据；不是任意 shell 或重放许可 | WVD 纯策略生成冻结计划，ExecutionSession 验证并调用离线 LifecyclePort | 每步核对身份/时效/连接代次和后置状态；停止仍由原 Run 持有资源 |
| snapshot.business.lifecycle_recovery_active/sequence | 本 Run 恢复请求边界，不是跨进程检查点 | WvdRunState 在 LifecycleRecovery 边界和新帧 game_restarted 确认时更新 | 区分同次升级与启动成功后的新故障；不以曾有 lifecycle 计划推断当前仍在恢复 |
| result.json snapshot.business、sessions[].business/checkpoint | 静止边界的业务摘要及根检查点证据，不是崩溃后自动续跑许可 | RunCoordinator 在 Session join 后拷贝 | 历史审查与离线验收；completed_business_units 单独计数，不把一个段当整条任务 |
| result.json snapshot.sessions[].inputs | 每个已静止代次的尝试、接受、拒绝、底层输入与清理计数，不受有界事件淘汰影响 | RunCoordinator 收集 SessionResult 时复制 | 与 Run 总计核对；schema1 的新增可选摘要，旧结果没有此字段时标记未记录，不推导为零、不改写历史 |
| WvdRunState wall_bypass_step/sequence | 重启后防空气墙动作的已确认阶段及本轮序号，不是移动成功或物理引擎恢复证明 | 游戏重启清阶段并换序号；真实有限子图的三个确认事件顺序推进 | 路线恢复后继续未完成阶段；bypass_after_restart 是派生布尔值，旧回执不作用于新重启 |
| WvdRunState trap_unit / trap_cycles_completed、陷阱任务的 dungeons | 专项本轮归属、确认完成数及旧开始次数口径，三者不等价 | 陷阱图在已知本内场景开始时增加开始次数，七点完成才提交完成回执 | 同轮恢复不重计开始；普通下一轮另计，提前回城不能 Completed；当前实现待专项离线验收 |
| WvdRunState giant_unit / giant_route_completed / giant_cycles_completed | 巨人任务当前周期、路线确认与完整周期完成；不是同一个计数 | 巨人有限图分别在开始、两点完成、回城及应有住宿后确认 | 同周期恢复保留路线/付款回执，间隔采用旧 REST_INTERVEL+1；不从历史结果自动重放 |
| Gateway识别缓存 unknown.window | 单会话未知画面的上一灰度帧、九个差值和有界采样状态；不是游戏状态权威 | 纯视觉unknown_frozen识别写入；已知页面清空、Gateway关闭释放 | 地下城入口/分派判断是否请求恢复，只输出有界诊断字段；不授权点击或跨代次复用 |
| WvdRunState 确认签名、business.confirmed 事件、snapshot.business.last_confirmation | 本 Run 的操作去重与确认诊断；不是跨进程恢复数据库 | WvdConfirm 在实际新帧识别后生成；状态最多保存 4096 个签名，EventJournal 有界 | 任务步/宝箱计数与终态审查；摘要不等于持久化 profile 回执，不允许据此启动实机重放 |
| snapshot.business.inn_rest_completed/inn_rests/supply_cycle、last_bag_clear | 本 Run 已确认住宿与队伍周期；不是装备或游戏资产权威 | 新帧确认后由 WvdRunState 更新，入本开始新补给周期 | 回城补给和旅店子图避免已确认住宿重付；跨普通段/恢复代次保留，不从磁盘历史自动重建许可 |
| snapshot.business.inn_payment_pending | 本 Run 在付款前记录的待确认意图，不证明已付或未付 | 旅店子图的 inn_payment_prepared 确认置位，付款后新帧 inn_rest_completed 清除 | 跨停止/恢复保留；导航、旅店与恢复策略阻止重付，未确认前入本也拒绝，不从历史文件自动重放 |
| WvdRunState prepared 选择及 business.combat 事件 | 本代次未完成技能意图和已确认消费诊断；不是恢复许可 | WvdCombat 在同帧头像匹配后选择；新帧后置确认后消费 | 技能图读取只读摘要并重新识别原角色；不保存旧帧/坐标，段边界清除未完成选择 |
| snapshot.business.combat_sequence/chest_sequence、revival_sequence/pending/revivals | 本 Run 遭遇身份与复活确认；序号不是成功统计，也不授权重放 | WvdRunState 根据新帧 WvdConfirm 事件更新 | 失败遭遇不重用 ID；复活图和外层恢复读取，已有成功计数不倒扣；不是跨进程断点 |
| WvdRunState 的 chest::Selection、snapshot.business.chest_character/available_mask/character_attempts | 当前宝箱候选池与选择意图，不含坐标或图片 | WvdChest 同帧恐惧观察更新候选池，受控输入后由 WvdConfirm 登记尝试 | 开箱图只读选路；切换代次清意图，新箱重置候选池；编号不能代替新帧点击许可 |
| snapshot.business.death_prompt_sequence/death_prompt_pending | 本 Run 队友死亡提示的观察/清除回执，不等于复活结果 | 通用阻塞消费者经新帧 WvdConfirm 更新；首次观察重置策略，重复观察不重置 | 普通插入返回后核对清除；不修改胜场、复活或持久化配置 |
| snapshot.business.suicide_requested/party_defeat_sequence | 对应旧 _SUICIDE 置位事实及其观察序号；不是新的自杀动作策略 | 多人死亡提示 WvdConfirm 置位，RiseAgain 的已确认复活复位；游戏重启保留 | 仅摘要/诊断消费；固定旧源未读取此字段来决定战斗动作，新版同样不凭它新增输入 |
| docs/migration/m4-implementation-map.json、m4-task-status.json | 当前实现叠加和完整任务分母；不是执行计划 | m4_inventory.py 对照固定索引与实际数据测试结果 | 审核；DATA_BOUND_ONLY 不能解释为任务已运行 |
| .local/m3fix-m4-*、m3-fixes-*、m4-data-* | 本轮隔离配置、合成图、失败复现与测试结果 | 工作包执行脚本、原生检查入口 | 私有审核证据；公开包仅收脱敏报告/索引，不包含配置原文或真实图片 |

## 一致性与边界

- 专用对话策略属于冻结CompiledWorkflow及其WvdVision binding，不是GUI配置或
  全局运行变量；同一资源revision覆盖选项和关闭图。RunState唯一持有专用对话
  待确认意图、序号与完成数，WvdConfirm写入、公共处理/恢复读取；不代表悬赏已交付。

- `snapshot.business.fishing`由WvdRunState内Fishing Progress唯一持有：待确认分类、
  收获页序号、已关闭后的鱼数/分类、等待起点的派生超时和失败数；以及补饵阶段、
  转交待确认意图、已确认输入数（不是物品数）、完成返钓次数。
  新帧WvdConfirm生产，钓鱼图与结果审查消费；恢复不清待确认收获、转交意图和计时。
  这是离线Run状态，不是背包权威或可执行的跨进程恢复点。

- `snapshot.business.bounty_cycle`由WvdRunState内BountyCycle持有，区分跳跃、跨城、
  揭榜、两条路线/返回、交付和住宿阶段。图中的新帧确认推进；每阶段校验所属正常段，
  交付份数读取同一bounty_reports权威。转场待确认时恢复策略不重发，结果仅供审查，
  不允许从历史文件自动恢复领取、交付或付款。

- `snapshot.business.sandman`由WvdRunState内SandmanCycle唯一持有：attempts为寻缘访问次数，
  completed_cycles仅在缘专用选项回执、两次独立住宿和两次跳跃全部确认后累计。
  当前阶段与leap_pending由WvdConfirm更新，任务图和恢复策略消费；历史摘要只读，
  不允许据它重放跳跃或付款，也不是账号缘等级的权威数据。
- `snapshot.business.gold_income`由GoldIncomeCycle持有，记录十段剧情的当前阶段/待确认意图。
  完整剧情终点才产生每次7000的估算收益，不是账号余额。`snapshot.business.bull_cave`
  由BullCaveCycle持有，记录两/三正常段、路线、跳跃意图；付款仍由原住宿回执权威持有。
  两者均由WvdConfirm生产、任务/恢复决策和结果审查消费，不从历史摘要重放账号操作。
- 因果滚动的BGR参考ROI只存在当前Gateway的RecognitionCache，固定每方向一份，
  Session销毁即释放；不进入WvdRunState、profile或RunStore，不作为跨代次恢复点。
- `snapshot.business.steel_trial`由WvdRunState内SteelTrial持有，记录源码扩展的选择意图、
  四点路线、返回与住宿阶段；WvdConfirm是生产者，专项图、恢复策略和结果审查为消费者。
  选择待确认跨恢复保留；completed_cycles只表示本次离线业务终点，不是账号试炼成绩。
  扩展任务由显式提供的新目录加载，基础58项目录不自动增补，历史结果不用于自动续跑。
- `snapshot.business.repel_forces`由WvdRunState内RepelForces持有：每组双战进度、开战意图、
  已观察战斗、完成战斗和周期数。WvdConfirm按新帧生产，专项图/恢复策略/结果审查消费。
  只有战斗出现后返回指定对话才计数，未确认意图禁止生命周期恢复后重发。
  住宿付款沿用同一inn回执权威，不另外记录账号资源；历史摘要不允许自动续跑。

- `snapshot.business.fordraig/cave_of_separation`分别由WvdRunState中的类型化周期状态持有。
  WvdConfirm依据新帧生产阶段、机关/领取/跳跃意图与业务回执，续段和恢复读取同一对象。
  Fordraig非boss自动战斗是当前阶段派生值，不覆盖冻结profile或原策略；
  分离洞窟的途中停点与真正任务完成分别记录，不能把Ena/请求对话出现当成整轮完成。
- 多阶段发布的`pipeline/stageN.json`属于同一个派生封存包，所有正常段共享revision，
  不存在每段的第二份资源权威。各Session只拥有本段入口/终点/对话绑定；
  活动期不得修改任意阶段文件或补图，详见`m4-stage-publication-validation.md`。

- `snapshot.business.sleep`由WvdRunState内的SleepVisits唯一持有，记录固定9999次目标、
  当前住宿意图及完成数；每次退出旅店的新帧确认后递增。`tasks::configure_sleep_units`
  只定义250个有限正常段，不新增调度器。已完成批次的住宿签名在正常换批时释放，
  同批次恢复保留；该事实不改变旧配置或允许从历史文件重放付款。
- `snapshot.business.bounty_reveals/bounty_reports/bounty_report_pending`为揭榜退出、
  已确认交付次数和交付前意图；WvdConfirm生产，悬赏图/恢复策略与离线审查消费。
  揭榜不是指定任务已接取，交付按钮可见也不是报酬到账，不能混用这三个字段。

- `prepare_pipeline_bundle` 显式新目录及其 `parameters/template-expansion.json` 是准备阶段的派生资源，
  由storage持锁复制作者文件、冻结目录模板顺序并生成新revision；M4只读检查CLI/离线运行消费。
  不是正式账号数据或历史恢复点，不覆盖源目录，只有新revision可进入随后冻结的Run权限。
- `snapshot.business.dark_light_active` 是当前Run暗灯阶段；恢复保留、回城确认清除。
  `encounter_timed_out` 是未结算遭遇计时的只读派生值，不是另一份计时权威或输入许可。
- `snapshot.business.mining` 由WvdRunState内的mining::Progress唯一持有，包含奖励类别计数、
  奖励页序号/是否已关闭、补镐子意图和组队阶段。新帧WvdConfirm提交，运行结果只读保存；
  不写旧配置/背包或复制SDK对象。重复奖励页不再次累计，住宿确认前补给意图不得清除。

- SDK 对象、帧、识别缓存不是数据权威，不反写源图片或模型。
- WvdConfirm、WvdCombat 和 WvdChest 修改运行状态前复核当前观察的代次、epoch、应用与帧龄；识别曾经 Hit 不等于提交时仍有效。此检查不创建输入许可，过期不得计数或写回 profile。
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
