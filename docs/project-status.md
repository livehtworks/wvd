# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `src/script.py` 中的 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死到本机路径。
- 图像识别基准为 900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，可使用既有 `mod` 扩展。没有项目数据库。
- 文本日志与诊断截图是运行证据，不能作为源码或分发资源提交。

## 当前阶段

- 同步目标为上游 2.8.7（`1c37b76`）；保留本地恢复、配置和日志契约。
- 新增近端/远端钓鱼、沙人缘、FFXI 左侧精英任务；合入挖矿、楼层与任务点策略切换修复。
- 策略新增“释放任一即完成”；恢复复选框改为正向显示，持久化 SKIP 字段含义不变。
- 保留全局策略重置和 SKIP 恢复字段，避免升级改变旧配置含义；未切换 loguru 或上游箭头算法。
- 本轮验收与待观察项目见 `upstream-sync-2.8.7.md`。未经实机完整任务验证的功能不得标为实机通过。

## 知识入口

- 执行注意项：`execution-notes.md`。
- 同步取舍和验收：`upstream-sync-2.8.7.md`。
- 历史稳定性改动与性能测量：`local-stability-and-performance-notes.md`（其中历史观察不替代本文件当前事实）。
