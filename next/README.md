# WVD Next · 迁移工作台与独立核心

独立 C++20 / CMake、Boost.Beast HTTP 服务与 Vue / TypeScript 迁移工作台。
**只做旁路工程与完整功能基线，不是可挂机的新版本。** 旧 Python 仍是唯一生产入口。

已实现：原生服务正常启停、API 版本/能力查询、基线项搜索与详情、资源大小写核对及真实模板预览。
另有显式 M3 本地只读设备检查和 WVD 专用视觉；这不是工作台的在线执行功能。
未实现：完整 WVD 任务、配置导入、流程编辑和生产替换。
尚未启用的工作包目录只放职责说明，不提供伪实现，不进入构建。

M1 收尾 R01/R02/R03 已完成。M2 核心已实现独立 RunCoordinator、有限 ExecutionSession、
统一 MaaGateway、模板/OCR 三态、受控动作/输入门禁及原子运行结果存储。
`wvd_core` 只在显式构建时加载 Maa，不链接进工作台服务；默认离线路线仍在 connect 前拒绝真实后端。
验收范围与证据见 [M2 核心验证](docs/m2-core-validation.md)，架构说明见下方导航。

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

```powershell
next/build/Release/automationd.exe --web-root next/web/dist --port 17652
```

控制台看到 READY 后访问 `http://127.0.0.1:17652`；Ctrl+C 正常停止。
端口占用时启动失败，不接管已有服务；可选择其他端口或 `--port 0`，以 READY 地址为准。
HTTP 仅绑定 127.0.0.1，严格验证 Host/Origin，只提供 GET/HEAD，不开放 shell/设备/任务 API。

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
