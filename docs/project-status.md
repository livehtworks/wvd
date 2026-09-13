# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `src/script.py` 中的 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死到本机路径。
- 图像识别基准为 900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，可使用既有 `mod` 扩展。没有项目数据库。
- 文本日志与诊断截图是运行证据，不能作为源码或分发资源提交。

## 当前阶段

- `WVD_MaaFramework_M2_Fix_and_M3_Pack_20260913` 本轮结论：Gate A PASS，M3_IMPLEMENTED_WITH_GAPS，停在 M4 前。最终 100 项 M2、23 项 M1、8 组 M3、4 个浏览器场景通过；另有 7 项实际入口绑定拒绝和全资源/设备图像核对。工作包交付时未提交/推送，随后用户追加授权提交到个人 fork；未打包或切换旧版。
- MaaFramework M0 限定交付已获用户审核接受并收口，继续保留该技术路线；不再重复 M0 内存测试。42 项离线原生断言、18 项汇总器检查及 285 组资源采样的范围结论保留，资源仍 RESOURCE_UNRESOLVED，不放行生产切换。
- 独立 `next/` C++20/CMake + Vue/TypeScript 工程与完整功能盘点已完成；M1 收尾修复 HEAD 错误响应、战斗识别归属及刷新陈旧详情/页码，23 项服务/盘点测试和 4 个浏览器场景通过并独立封存，见 `../next/docs/m1-fix-validation.md`。
- 基线盘点覆盖 250 函数、33 配置、58 任务、82 入口，含 784 条 if/case 分支与嵌套字段。静态索引不变，M3 的 28 项纯视觉/混合函数去向单列于 m3-implementation-map.json；实现存在不等于真实质量通过，58 个完整任务未迁移。
- M2 当前核心包括统一异常收尾、最终像素边界和封存行为定义；最终真实 SDK 离线验收 100 项通过。原 85 项没有删除或减弱，前阶段报告 `../next/docs/m2-core-validation.md` 保留为历史，本轮以 `../next/docs/m2-fix-validation.md` 和 M3 报告为准。
- `wvd_core` 只由显式 M2/M3 构建加载，M1 服务仍不链接 Maa、不接设备。默认离线路线在 connect 前拒绝真实后端；M3 专用入口重新核对私有 MuMu 绑定，通过同一协调器、Session、Gateway 和输入门禁执行有限只读截图。
- M3 新增 MuMu EmulatorExtras/ADB Encode 单 Controller 顺序后备、真实帧来源/连接代次、实测系统只读 viewport、固定 OpenCV 专用视觉与 437 张原字节资源副本。公开 manifest 包含 8 处引用的显式别名及 44 处动态引用去向，用户 mod/配置未导入。
- M3 的真实 NEXT/Pause 质量、旧 Tesseract 等价性及原生阻塞取消边界仍开放；没有系统安全场景证明时不进行导航输入。SDK 内部 KillServer 已通过公开配置替换为只读 get-state，内部重试等待仍存在，不能说已彻底禁用内部恢复。
- 实机只启动一个原本关闭的既有 MuMu 实例；1600×900 Android 桌面 IPC/Encode 各 5 预热+50 正式全部成功，一次正常连接重建成功，实际输入为零。前台查询由 Android 15 不含焦点的 windows 子段修正为完整 window 报告。结束留在桌面，未开启游戏/VPN；系统导航仍为 BLOCKED。
- 完整视觉包离线识别中位数 532.93ms，小包 32.75ms；主要成本是两处完整包校验。保留完整性语义，不宣布性能放行。M0 RESOURCE_UNRESOLVED 仍未归因。
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
- 本轮 M2 修正与 M3 阶段报告：`../next/docs/m2-fix-validation.md`、`../next/docs/m3-device-vision-validation.md`。
