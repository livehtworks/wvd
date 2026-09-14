# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `src/script.py` 中的 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死到本机路径。
- 图像识别基准为 900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，可使用既有 `mod` 扩展。没有项目数据库。
- 文本日志与诊断截图是运行证据，不能作为源码或分发资源提交。

## 当前阶段

- 要塞八层陷阱专项检查点 `492322c` 首次构建及状态 19 方法通过；依赖的自动移动地图流程首轮 3 方法在 Auto 后置帧超过 TTL 时失败，尚未到开图。正在收敛纯后置分类，不放宽 TTL；见 `../next/docs/m4-auto-map-validation.md`、`../next/docs/m4-fortress-trap-validation.md`。善恶阶段共用/全局提示/开箱回归 24 方法已全部通过，不与新源码混用证据。

- 善恶选择与新版 profile 写回已接通，状态 18、流程 6 方法/13 场景、配置数据 11 方法通过；见 `../next/docs/m4-karma-validation.md`，实现检查点 `d5404bd`。纯规则、状态确认和存储 CAS 分层；保存失败不重发动作，未知后置保留门禁 Failed；不是完整任务/跨进程恢复通过。

- 沙人/祝福提示已接通：计划 6、流程 7 方法/11 场景、同产物通用/Pause/死亡回归 22 方法通过，见 `../next/docs/m4-global-prompt-validation.md`，实现检查点 `f009420`。重启后空气墙动作阶段状态 17、流程 5 方法/8 场景通过，见 `../next/docs/m4-wall-bypass-validation.md`；不将动作确认当作物理引擎恢复证明。
- 普通/快速开箱完整有限链已通过：状态 14、流程 9 方法/18 场景、计划 6 方法；原 240 秒预算失败同例修后完成 40 次输入，见 `../next/docs/m4-chest-selection-validation.md`。实现检查点 `2912988`，不将此子流程记为完整 TaskID 通过。
- 队友死亡提示：状态 15 方法通过；首次后置识别超 2 秒 TTL 失败，收敛专用候选后，死亡提示与父流程预算 6 方法/16 场景通过。同产物通用阻塞/Pause 回归 12 方法通过；实现提交 `2754096`。见 `../next/docs/m4-party-death-validation.md`、`../next/docs/m4-parent-budget-validation.md`。多人死亡提示状态 16、流程 5 方法/6 场景通过，同产物单人提示回归 5 方法/12 场景通过，见 `../next/docs/m4-party-defeat-validation.md`，实现检查点 `c2c0386`。
- 资源封存补验 2 方法/新增 9 负例全部通过，含写锁冲突、中途失败释放、目录改名/新增和 junction；提交 `1564f85`。见 `../next/docs/m3-integrity-negative-validation.md`，不改变 metadata、资源稳定性和完整矩阵剩余边界。

- Pause 覆盖层先于底层就绪图标处理，六次无效点击后新帧仍 Pause 才请求恢复；首次/第六次成功和角色/详情负例通过，补验及普通阻塞/启动恢复 21 方法通过，见 `../next/docs/m4-pause-validation.md`，实现提交 `a8b1bd1`。
- 复活消费者与失败遭遇独立编号已接入正常路线：状态 13、复活流程 5 方法/9 场景、计划 6 方法通过，见 `../next/docs/m4-revival-validation.md`，提交 `04999ca`。包括失败后复活、恢复角色并继续到任务点；不计失败战斗为胜场。该阶段产物 Pause 4 方法回归通过，未混用不同 EXE 身份。

- 地图/Auto/角色回合/遭遇/宝箱/角色恢复已接通普通阻塞中断与外层重识别，5 方法/6 场景通过，见 `../next/docs/m4-interruption-validation.md`，阶段提交 `6e24743`。前阶段共用回归 64 方法中 63 通过，唯一恢复子图总预算失败已修后同例通过。入城/住宿/专项和全局事件仍未齐。
- 副作用动作后覆盖层的独立“结果未确认”出口及恢复策略拒绝已通过定点验证，见 `../next/docs/m4-effect-interruption-validation.md`；不把此状态记为成功，不自动重放或重启。该批 11 方法全部通过（含 5 项图片来源及恢复预算复验），同产物 M2 102、M3 定点 9、计划 6 方法回归通过；完整逐任务验收仍未齐。
- 新版 profile 原生并发/锁冲突/替换失败和配置全字段区段边界已验证：数据 11、状态与存储 12 方法通过，见 `../next/docs/m4-profile-boundaries-validation.md`，提交 `f5b1e9c`。图片 mod 持锁发布复制与统一选择顺序五项通过，见 `../next/docs/m4-image-import-validation.md`；输入帧 TTL 不变。
- 普通启动阻塞已在迭代入口/路线分派接通：8 方法/12 场景、M3 定点 3、启动恢复 11、计划 6 方法通过，见 `../next/docs/m4-common-screen-validation.md`。正常迭代曾有 1 项自动退场因输入前帧过期失败；内部原生动作默认延迟已改为本次节点显式时序，该用例补验通过，完整 M2 102 项及 M3 定点 3 项通过，见 `../next/docs/m4-native-action-timing-validation.md`。子动作中途弹窗打断仍待接齐。
- 正常 Farm 迭代图已接入入口/补给/EOT/路线及正常续段：43 项静态编译绑定核对、5 方法/8 场景通过；导航 12、状态 11、核心定点 15 方法回归通过，见 `../next/docs/m4-iteration-validation.md`。两段共享统计且再次入本按配置重置策略。全局事件和完整逐任务验收尚未齐，仍 0/58。
- M4 已接入配置副本/profile-CAS、58 项类型化任务数据、运行级状态工厂、策略消费、正常续段、43 项入本/路线图。回城普通/强制补给、角色面板恢复、住宿回执和退场新帧确认已组合；证据见 `../next/docs/m4-departure-validation.md`、`../next/docs/m4-dungeon-route-validation.md`、`../next/docs/m4-healing-validation.md`。
- 战斗已有头像/技能/等级/敌我目标、防跨角色连点、Auto 确认及有界持续等待、有限多角色遭遇和宝箱转战斗。原生子作用域仅清理局部命中预算，不清父预算/策略；证据见 `../next/docs/m4-combat-turn-validation.md`、`../next/docs/m4-auto-wait-validation.md`、`../next/docs/m4-encounter-validation.md`、`../next/docs/m4-native-child-validation.md`。
- 类型化恢复已有应用优先、重连/实例升级、启动后继续原任务的离线链。首次连接失败冷启动 6 方法/7 场景、恢复全组 17 方法、M2 102、M3 定点 11、状态 16 方法通过，见 `../next/docs/m4-cold-start-validation.md`；不包含后续沙人/祝福提示改动。完整任务恢复位置、全部全局事件和真实端口未齐。
- 当前主要剩余：死亡/其它全局对话及普通插入全链连接，15 项专项，副作用确认/保存间的对账与新版 profile 写回，配置/mod 剩余组合及资源导入，逐任务离线成功/失败/停止/恢复和最终完整性矩阵。已通过的开箱、Pause 和复活子链仍须全任务组合验收。分项事实见 `../next/docs/m4-business-validation.md` 与 `../next/docs/migration/m4-task-status.json`；不把子图、静态编译或正常迭代通过改成完整 TaskID 通过。
- 阶段历史、失败修正和 EXE/SDK 身份以各专题报告为准，不累计旧测试数量冒充当前全量复测；早期私有证据和原审核包保持不变。输入帧 TTL 未放宽，Session 总预算随编译定义封存。

- 当前 `WVD_MaaFramework_M3_Fix_and_M4_Pack_20260914` 整体未完成：FIXES_INCOMPLETE；M4 为 M4_PARTIAL_IMPLEMENTATION。起点 `4d7c4fa`，初始干净；本次按用户最新授权将累计实现保存为本地检查点，后续每个阶段验证后及时 commit。提交不代表整包验收通过；没有真实设备查询/连接/输入、旧版打包、push/PR 或生产切换，不进入 M5。
- ROI 调用范围、Custom 条件/动作门禁、boolean 组合条件和活动资源快照已有针对性断言。M3 发现查询句柄差异及 CLEANUP_PENDING 调用者所有权仍阻断，完整性负例矩阵未齐，不能沿用旧 Gate B 放行。
- 工作包初始私有根为 `next/.local/m3fix-m4-9c6409e289f74725932570702eeaee04/`，保留初次失败和固定窗口；保护基线包含 450 个文件。当前任务台账仍保留全部分母，不把数据或子流程通过当完整实现。
- 前一工作包 `WVD_MaaFramework_M2_Fix_and_M3_Pack_20260913` 的 Gate A PASS / M3_IMPLEMENTED_WITH_GAPS 是历史阶段结论。随后用户授权提交到个人 fork；当前代码与验收以本轮报告为准，未切换旧版。
- MaaFramework M0 限定交付已获用户审核接受并收口，继续保留该技术路线；不再重复 M0 内存测试。42 项离线原生断言、18 项汇总器检查及 285 组资源采样的范围结论保留，资源仍 RESOURCE_UNRESOLVED，不放行生产切换。
- 独立 `next/` C++20/CMake + Vue/TypeScript 工程与完整功能盘点已完成；M1 收尾修复 HEAD 错误响应、战斗识别归属及刷新陈旧详情/页码，23 项服务/盘点测试和 4 个浏览器场景通过并独立封存，见 `../next/docs/m1-fix-validation.md`。
- 基线盘点覆盖 250 函数、33 配置、58 任务、82 入口，含 784 条 if/case 分支与嵌套字段。静态索引不变，M3 的 28 项纯视觉/混合函数去向单列于 m3-implementation-map.json；实现存在不等于真实质量通过，58 个完整任务未迁移。
- M2 当前核心包括统一异常收尾、最终像素边界和封存行为定义；最终真实 SDK 离线验收 100 项通过。原 85 项没有删除或减弱，前阶段报告 `../next/docs/m2-core-validation.md` 保留为历史，本轮以 `../next/docs/m2-fix-validation.md` 和 M3 报告为准。
- `wvd_core` 只由显式 M2/M3/M4 构建加载，M1 服务仍不链接 Maa、不接设备。默认离线路线在 connect 前拒绝真实后端；M3 设备入口当前因发现链阻断不可放行。
- M3 新增 MuMu EmulatorExtras/ADB Encode 单 Controller 顺序后备、真实帧来源/连接代次、实测系统只读 viewport、固定 OpenCV 专用视觉与 437 张原字节资源副本。公开 manifest 包含 8 处引用的显式别名及 44 处动态引用去向，用户 mod/配置未导入。
- M3 的真实 NEXT/Pause 质量、旧 Tesseract 等价性及原生阻塞取消边界仍开放；没有系统安全场景证明时不进行导航输入。SDK 内部 KillServer 已通过公开配置替换为只读 get-state，内部重试等待仍存在，不能说已彻底禁用内部恢复。
- 上一 M3 阶段曾验证单个既有 MuMu 的桌面 IPC/Encode 截图及一次连接重建，实际输入为零，前台查询改为完整 window 报告。该历史现场不代表本轮仍有效的许可；本轮没有查询或操作设备，系统导航仍为 BLOCKED。
- 本轮固定前后窗口中，完整包离线识别中位从 562.00ms 降至 36.81ms，小包从 32.91ms 降至 24.49ms；初始化全包从 488.02ms 增至 1224.03ms，活动 lease 持有 441 文件和 23 目录句柄。热调用不再重复文件 hash，但仍遍历成员；峰值内存和完整矩阵未齐，PERFORMANCE_UNRESOLVED 与 M0 RESOURCE_UNRESOLVED 均保留。
- M0 的 OCR Unset 历史结果不改写；新增适配层已测结果单列。真实 NEXT/Pause、原生等待可中断性、资源归因及 Spark 边界仍开放，原 M0 限定结论见 `maafw-m0-review-resolution.md`。
- 同步目标为上游 2.8.7（`1c37b76`）；保留本地恢复、配置和日志契约。
- 新增近端/远端钓鱼、沙人缘、FFXI 左侧精英任务；合入挖矿、楼层与任务点策略切换修复。
- 策略新增“释放任一即完成”；恢复复选框改为正向显示，持久化 SKIP 字段含义不变。
- 保留全局策略重置和 SKIP 恢复字段，避免升级改变旧配置含义；未切换 loguru 或上游箭头算法。
- 本轮验收与待观察项目见 `upstream-sync-2.8.7.md`。未经实机完整任务验证的功能不得标为实机通过。

## 知识入口

- 执行注意项：`execution-notes.md`。
- 同步取舍和验收：`upstream-sync-2.8.7.md`。
- 历史稳定性改动与性能测量：`local-stability-and-performance-notes.md`（其中历史观察不替代本文件当前事实）。
- 独立 C++/MaaFramework 探针、脱敏结论与审核说明：`../validation/maafw-windows-20260913/`；不参与 WVD 构建、加载或运行。
- M0 最新边界与外部验证证据索引：`maafw-m0-review-resolution.md`；历史快照不替代本轮证据结论。
- M1 工程命令与架构：`../next/README.md`、`../next/docs/architecture.md`；完整映射与业务约束：`../next/docs/migration/README.md`。
- M2 运行文件与测试隔离的数据权威：`../next/docs/data-authority.md`。仅新增文件存储，没有数据库或正式新版配置入口。
- 当前 M3 修复与 M4 接续：`../next/docs/m3-fix-validation.md`、`../next/docs/m4-business-validation.md`、`../next/docs/m4-state-validation.md`、`../next/docs/m4-plan-validation.md`、`../next/docs/m4-workflow-validation.md`；资源所有权：`../next/docs/integrity-snapshot-contract.md`；字段和完整任务分母位于 `../next/docs/migration/m4-*`。
- 历史 M2/M3 报告：`../next/docs/m2-fix-validation.md`、`../next/docs/m3-device-vision-validation.md`，不覆盖当前阻断。
