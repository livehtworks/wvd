# WVD Next Windows 工作台

当前候选使用 C++20、Vue、OpenCV、ONNX Runtime、MuMu IPC、ADB 和 scrcpy 控制协议；正式服务不加载 MaaFramework。旧 Python 程序及 `next/dist/wvd-next` 均未替换。

双击 [启动WVD原生版.bat](dist/wvd-next-native/启动WVD原生版.bat) 打开工作台。新版配置与作者流程写入 `%LOCALAPPDATA%/WvdNext`，不会覆盖旧 `config.json`、`mod` 或日志。首次使用请在工作台核对模拟器路径、实例编号、ADB 地址和 VPN 选项；游戏任务效果尚未按新设备后端完成实机验收。

在仓库根目录构建和执行有限离线验收：

```powershell
python next/tools/build.py
python next/tools/validate.py
```

需要 VS 2022 x64 C++/CMake、锁定的 Node/npm 版本。脚本校验固定依赖 SHA256、构建 Vue 和唯一原生服务，输出到 `next/dist/wvd-next-native`。验收只运行直接相关的原生机制和正式 `Application` 作者/任务入口，不连接 MuMu，不执行游戏动作。完整日志在 `next/.local/logs`。不要用离线通过代替游戏任务通过。

当前架构与限制见 [架构](docs/architecture.md)、[工作包执行记录](docs/native-removal-acceptance.md)和 [项目状态](../docs/project-status.md)；旧 Maa 阶段资料在 `docs/archive/` 与 `archive/`，不再是构建入口。
