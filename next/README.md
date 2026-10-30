# WVD Next Windows 工作台

当前产品使用 C++20/CMake、Vue、OpenCV、ONNX Runtime；设备通过 MuMu IPC 取图、ADB 查询及生命周期控制、scrcpy 输入。Maa 不参与当前构建和正式运行。

## 使用入口与当前部署

当前正式候选为 `next/dist/wvd-next-native/`，端口 17654，data-root 为 `D:/programcode/python/wvd/next/.local/c11-flow-product/data`。候选当前已停止，200 轮目标为 24/200、剩 176；具体现场以[项目状态](../docs/project-status.md)为准，文档维护不启动服务。

`启动WVD原生版.bat` 使用原 `manage_service.ps1`。首次 Start/Deploy 必须显式提供 data-root；后续可复用候选 `service-launch.json` 中仍存在的绑定目录。**没有 AppData 默认目录，也不自动重建失效绑定。** `profile.json` 和 `workflows/` 为用户权威，不能用旧 `config.json`、包内默认或测试配置覆盖。

MuMu 当前安装在 `F:/Applications/MuMuPlayer`；原实例 0/1/2 及游戏数据保留。WVD 绑定实例 2，ADB 为 `127.0.0.1:16448`，原 VPN 选项保持；新安装环境先按自己的实际设备配置核对。

## 构建与验收

在仓库根目录执行：

```powershell
python next/tools/build.py
```

构建需要 VS 2022 x64 C++/CMake 和锁定 Node/npm；`build.py` 沿原入口校验依赖、生成资源、构建 Vue/原生服务及打包。Release 输出在 `next/build/Release/`，打包由 `next/tools/package_functional.py` 负责。默认输出不授予覆盖活动候选权限。

按[执行注意项](../docs/execution-notes.md)配置项目内缓存和输出；只运行受影响范围的本地验收，测试 data-root 必须独立可丢弃。仅修改文档不重跑构建或游戏，不使用线上 CI。

## 文档与维护

- [项目文档导航](../docs/README.md)、[维护入口](../docs/maintenance.md)。
- [当前架构](docs/architecture.md)、[核心合同](docs/core-contracts.md)、[数据职责](../docs/data-authority.md)。
- [旧阶段摘要](docs/history/stage-verification.md)、[工程历史摘要](../docs/history/engineering-summary.md)。

旧 Python、`mod`、旧配置和历史用户资产保留其原职责。Git 已保存的旧阶段报告不继续堆在当前目录，当前测试素材按真实调用关系保留。
