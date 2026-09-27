# WVD Next Windows 工作台

当前候选使用 C++20、Vue、OpenCV、ONNX Runtime、MuMu IPC、ADB 和 scrcpy 控制协议；正式服务不加载 MaaFramework。旧 Python 程序及 `next/dist/wvd-next` 均未替换。

双击 [启动WVD原生版.bat](dist/wvd-next-native/启动WVD原生版.bat) 打开工作台。新版配置与作者流程写入 `%LOCALAPPDATA%/WvdNext`，不会覆盖旧 `config.json`、`mod` 或日志。首次使用请在工作台核对模拟器路径、实例编号、ADB 地址和 VPN 选项；游戏任务效果尚未按新设备后端完成实机验收。

正式产品导航只有工作台和流程编辑。运行状态和停止入口由 App 常驻；配置分为常用参数、战斗方案、设备与高级，迁移盘点不再参与产品构建和发布。

在仓库根目录构建：

```powershell
python next/tools/build.py
```

需要 VS 2022 x64 C++/CMake、锁定的 Node/npm 版本。脚本校验固定依赖 SHA256、构建 Vue 和唯一原生服务，输出到 `next/dist/wvd-next-native`。具体任务只运行该轮对应的窄验收，不默认执行旧全量 `validate.py`。ddc3366 收束的隔离端口、独立候选及命令见[收束报告](../docs/reviews/closure-ddc3366-result-20260927.md)，不得指向正在使用的服务。离线通过不等于游戏任务通过。

历史迁移报告原文在 [冻结归档](docs/archive/migration-baseline-ddc3366/ARCHIVE.md)。`tools/inventory/generate.py` 仅供人工审计固定旧基线，输出到 `next/.local/audit/migration-baseline/`；不再向 web/public 写报告或复制预览图片。

当前架构与限制见 [架构](docs/architecture.md)、[工作包执行记录](docs/native-removal-acceptance.md)和 [项目状态](../docs/project-status.md)；旧 Maa 阶段资料在 `docs/archive/` 与 `archive/`，不再是构建入口。
