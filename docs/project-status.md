# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `src/script.py` 中的 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死到本机路径。
- 图像识别基准为 900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，可使用既有 `mod` 扩展。没有项目数据库。
- 文本日志与诊断截图是运行证据，不能作为源码或分发资源提交。

## 当前阶段

- MaaFramework M0 限定交付已获用户审核接受并收口，继续保留该技术路线；不再重复 M0 内存测试。42 项离线原生断言、18 项汇总器检查及 285 组资源采样的范围结论保留，资源仍 RESOURCE_UNRESOLVED，不放行生产切换。
- 用户随后明确授权 M1，独立 `next/` C++20/CMake + Vue/TypeScript 工程与完整功能盘点已完成；原生服务只提供版本/能力查询与只读迁移工作台，未链接 Maa、未连接设备、未切换生产。18 项原生服务/盘点测试及 2 项浏览器场景通过，见 `../next/docs/m1-validation.md`。
- 基线盘点覆盖 250 函数、33 配置、58 任务、82 入口，含 784 条 if/case 分支与嵌套字段；资源报告记录 8 处大小写差异和 44 处动态引用。所有迁移项仍为 MAPPED_NOT_IMPLEMENTED，不代表业务已迁移。
- 当前停在 M2 之前。OCR 三态缺口为 M2 落实项；真实 NEXT/Pause、原生等待可中断性、资源归因及 Spark 边界仍开放。详见 `maafw-m0-review-resolution.md`。
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
