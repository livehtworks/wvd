# WVD Next · Windows 自动化工作台

独立 C++20 / CMake、MaaFramework、Boost.Beast 与 Vue / TypeScript Windows 应用。
当前交付已接通配置、MuMu、截图、现有任务、运行控制、诊断和可保存执行的流程编辑器；
旧 Python 版本仍保留，用户自行选择何时使用新版。

直接使用打包产物：

```text
next/dist/wvd-next/启动WVD新版.bat
```

新版配置和流程保存在 `%LOCALAPPDATA%\WvdNext`，不会覆盖旧 `config.json`、`mod` 或日志。
具体操作和有限实操结论见 [Windows 功能交付](docs/windows-functional-delivery.md)。

流程编辑器内可把公共步骤/流程块作为调用节点复用：调用参数只影响当前实例，命名插槽用于按顺序
增加任务专用步骤；“展开 / 编辑公共定义”进入定义后可用“返回调用者”恢复原节点。保存和运行
使用同一作者图，运行开始时冻结完整引用闭包。当前 12 份公共定义只达到结构验证，既有任务仍走
原生入口，缺繁中公会资源的流程不会自动回退到英文。详见
[可组合流程状态](docs/composable-workflow-status.md)。

## 构建与验证

在仓库根目录执行，Python 3.11+ 仅用于构建/只读盘点，不会 import 旧程序：

```powershell
.venv-build/Scripts/python.exe next/tools/build.py
.venv-build/Scripts/python.exe -m unittest discover -s next/tests -v
```

需要 VS 2022 x64 C++/CMake 组件、Node 24.14.1 / npm 11.11.0。
构建脚本通过 PATH / vswhere 发现 CMake；不内置个人路径。固定依赖及 SHA256 见
`dependencies.lock.json`，Web 的完整依赖树由 `web/package-lock.json` 锁定，安装使用 `npm ci`。
头文件缓存默认 `%LOCALAPPDATA%/WvdNext/dependencies`，可用 `WVD_NEXT_CACHE` 显式指定。

开发运行需显式传入 Web、数据、资源包和任务目录；普通使用请直接运行打包入口。

控制台看到 READY 后访问 `http://127.0.0.1:17652`；Ctrl+C 正常停止。
端口占用时启动失败，不接管已有服务；可选择其他端口或 `--port 0`，以 READY 地址为准。
HTTP 仅绑定 127.0.0.1，严格验证 Host/Origin；设备和任务写操作只由同源工作台调用，不开放 shell。

浏览器验证（先启动上面的服务）：

```powershell
cd next/web
npx.cmd playwright install chromium
$env:WVD_NEXT_URL = 'http://127.0.0.1:17652'
npm.cmd test
```

安装过 Playwright Chromium 后，也可以在仓库根目录一键验收；它为浏览器测试创建独立端口，
结束后正常停止服务，不依赖用户手工打开预览：

```powershell
.venv-build/Scripts/python.exe next/tools/validate.py
```

重新构建前先正常停止本目录的 automationd，Windows 不允许覆盖运行中的 exe。
构建失败只报告日志，不自动终止进程。

构建日志在 `next/.local/logs`，浏览器截图/结果在 `next/web/test-results`，均不提交。
UI 开发可在 `next/web` 执行 `npm run dev`；同源完整验收以原生服务托管的构建产物为准。

## 导航

- [架构与依赖方向](docs/architecture.md)
- [可组合流程状态](docs/composable-workflow-status.md)
- [本地工作包收口](docs/composable-workflow-local-closure-20260923.md)
- [完整迁移基线与差异](docs/migration/README.md)
- [M1 验收报告](docs/m1-validation.md)
- [依赖与来源](docs/dependencies.md)
- [数据与运行文件权威](docs/data-authority.md)

M0 的 `RESOURCE_UNRESOLVED`、真实 NEXT/Pause 与原生阻塞取消边界仍保留；
M1 完成不消除这些限制，也不授权生产切换。

## M2 离线核心

先提供固定版本 SDK 压缩包和英文 OCR 模型目录；参数是本机显式输入，不内置任何用户目录。
SDK 与模型会校验 hash 后复制到被 Git 忽略的 `next/.local`，原有 M0 文件只读。

```powershell
.venv-build/Scripts/python.exe next/tools/prepare_maafw.py --sdk-archive <SDK压缩包> --ocr-source <英文模型目录>
.venv-build/Scripts/python.exe next/tools/build.py --m2-offline
.venv-build/Scripts/python.exe next/tools/validate.py --m2-offline
```

模型目录须含锁定的 `det.onnx`、`rec.onnx`、`keys.txt`。依赖版本不变，不自动降级或换模型。
测试使用既有隔离构建环境中的 OpenCV/NumPy 生成合成图片；不是新原生库的发布依赖。
默认 build/validate 仍只做 M1；显式选项额外启用离线目标，不启动游戏或新运行接口。

## M3 设备与视觉

**当前 M3 修复整体为 FIXES_INCOMPLETE：设备发现句柄/收尾问题阻断，完整性负例矩阵尚未齐全。
以下设备命令仅保留接口说明，本轮不得执行；不能沿用旧 Gate B 的通过结论。**

```powershell
.venv-build/Scripts/python.exe next/tools/build.py --m3
.venv-build/Scripts/python.exe next/tools/validate.py --m3
```

新增 OpenCV 直接编译依赖来自固定 MaaDeps 归档，准备工具核对 archive SHA256、头/库及与 SDK
同 hash 的 DLL；不升级 Maa 或 OCR。构建/离线验收不连接设备。生产配置和 mod 不导入。

在当前授权允许设备检查时，先只读发现，再显式启动受限检查：

```powershell
.venv-build/Scripts/python.exe next/tools/validate_m3_device.py --discover <旧配置路径> --binding <新建私有绑定文件>
.venv-build/Scripts/python.exe next/tools/validate_m3_device.py --binding <私有绑定文件> --capture-only
```

实例关闭且本轮授权允许启动时才加 `--start-instance`。程序会重新核对目标元数据和控制者，
不是信任历史编号。截图采用实测系统 viewport，零输入权限，不启动游戏/VPN、不领取任务。
`--safe-system-navigation` 尚无已确认的无副作用场景适配，明确返回 BLOCKED，不试点页面。
结果、原图和设备地址只写新建的 `next/.local` 私有目录；检查退出后不关闭用户实例。

修复与阶段边界见 [M2 收尾](docs/m2-fix-validation.md)、[M3 验收](docs/m3-device-vision-validation.md)。
**真实 NEXT/Pause 质量、长期资源归因和生产运行不因离线 PASS 自动放行。**

## M4 当前部分交付

```powershell
.venv-build/Scripts/python.exe next/tools/build.py --m4
.venv-build/Scripts/python.exe -m unittest discover -s next/tests/m4 -v
```

`--m4` 构建独立数据/状态/有限流程模块、`wvd_m4_check` 和真实 Maa 离线测试，不运行旧任务或连接设备。
完整 `validate.py --m4` 仍保留全部 M3 检查，已知失败不被跳过；数据测试通过不是整包通过。
台账包含 250 函数、33 配置、58 任务；58 项已做目录/类型化数据解析，业务运行通过数为 0。

- [M3 修复及性能证据](docs/m3-fix-validation.md)
- [M4 已完成部分与接续清单](docs/m4-business-validation.md)
- [M4 状态生命周期与验证](docs/m4-state-validation.md)
- [M4 任务类型化数据与验证](docs/m4-plan-validation.md)
- [M4 有限流程与实际输入验证](docs/m4-workflow-validation.md)
- [资源快照契约](docs/integrity-snapshot-contract.md)
- [配置和任务映射](docs/migration/m4-data-mapping.md)
