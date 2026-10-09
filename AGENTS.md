# 项目画像

- 本仓库是巫术 Daphne 自动化工具的个人维护 fork；当前产品在 `next/`，统一底座是 C++20/CMake、Vue、OpenCV、ONNX Runtime，Maa不参与当前构建。
- 唯一执行链为 Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有批次/线程入口，Coordinator拥有根运行与Session生命周期；禁止添加旁路执行器或外部循环守护器。
- 当前设备链是MuMu IPC取图、ADB元数据/生命周期、scrcpy输入；识别视口900x1600，语言由配置明确选择，不混扫英文与繁中。
- 当前data-root的 `profile.json` 和 `workflows/` 是原生配置/用户流程权威；`src/`、旧config、mod与dist保留为历史/用户资产，不参与原生运行，不得覆盖或清理。数据职责见 `docs/data-authority.md`。
- `origin` 指向原作者，`fork` 指向个人仓库；只有明确要求时提交或推送，推送明确指定个人远端。
- 当前事实入口：`docs/project-status.md`。执行构建、测试或诊断前读取 `docs/execution-notes.md`。
- 当前构建：在 `next/` 使用 `tools/build.py` 的依赖校验/配置/构建入口，Release在 `next/build/native/Release/`；冻结打包入口 `tools/package_functional.py`。构建、打包、部署和实机通过是四种独立事实。
- 定向离线回归：`python tools/run_contracts.py --module <runtime|devices|recognition|app|tools|web> --output <全新next/.local子目录>`；可先 `--plan`。修改模块不得静默省略对应回归，必须独立可丢弃data-root，不连接设备或默认服务端口。
- 核心合同见 `next/docs/core-contracts.md`；用户任务清单和当前代码仍是验收权威，不以全量测试数量替代真实业务交付。
