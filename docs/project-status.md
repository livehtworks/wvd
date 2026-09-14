# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `src/script.py` 中的 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死到本机路径。
- 图像识别基准为 900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，可使用既有 `mod` 扩展。没有项目数据库。
- 文本日志与诊断截图是运行证据，不能作为源码或分发资源提交。

## 当前阶段

- 新增有限入本与自动寻路：7 方法/12 场景通过，43 个 dungeon 入本图与资源核对通过；这里只是入本子流程，不改变 0/58 完整任务结论。重复识别的帧过期失败及两次有依据修正见 `../next/docs/m4-entry-validation.md`；门禁 TTL 未放宽。
- 当前 M4 已接入单角色有限战斗图：真实头像选策略、技能/等级/友方/敌方目标、防串角色点击、限定低置信、临时 Auto 确认和消费。11 个技能方法（13 个原生用例）、9 个状态及 3 个 M3 定点方法通过，见 `../next/docs/m4-combat-turn-validation.md`。已有地图、往返、补给、宝箱和只读状态分派/普通插入的阶段证据见 next/docs 内相应报告。外层整场战斗、完整任务分派及恢复仍未接齐，完整通过仍为 0/58；每阶段提交后继续，不作为整包停止条件。
- 前轮完整回归的历史证据在 `next/.local/m4-business-038717bea778422ba001a95ffec4e8c6/`：30 个 M4、100 个 M2、23 个 M1、4 个 M3 定点方法和 4 个浏览器场景，450 个保护文件未变。后续改动的针对性证据单列在上述报告，不将历史构建身份当作当前全量复测；前轮审核包保持不变。
- 运行级状态工厂、正常有限段续接、策略消费和 58 项任务类型化数据解析保持；前轮状态/计划证据在 `next/.local/m4-state-31d6be223ee5454daa24b726aad9c20c/`。子流程通过不代表完整任务已编译执行。

- 当前 `WVD_MaaFramework_M3_Fix_and_M4_Pack_20260914` 整体未完成：FIXES_INCOMPLETE；M4 为 M4_PARTIAL_IMPLEMENTATION。起点 `4d7c4fa`，初始干净；本次按用户最新授权将累计实现保存为本地检查点，后续每个阶段验证后及时 commit。提交不代表整包验收通过；没有真实设备查询/连接/输入、旧版打包、push/PR 或生产切换，不进入 M5。
- ROI 调用范围、Custom 条件/动作门禁、boolean 组合条件和活动资源快照已有针对性断言。M3 发现查询句柄差异及 CLEANUP_PENDING 调用者所有权仍阻断，完整性负例矩阵未齐，不能沿用旧 Gate B 放行。
- M4 已有离线配置副本导入、完整 profile/CAS、58 条任务目录及类型化数据、WvdRunState 和冻结状态工厂；策略按旧事件消费/重置，正常段续接与恢复分开。完整任务图、其余导航/补给、技能/目标/宝箱、类型化恢复、15 项专项和确认后业务写回仍未完成；58 项任务执行通过数为 0。
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
