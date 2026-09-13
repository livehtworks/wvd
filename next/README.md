# WVD Next · M1

独立 C++20 / CMake、Boost.Beast HTTP 服务与 Vue / TypeScript 迁移工作台。
**只做旁路工程与完整功能基线，不是可挂机的新版本。** 旧 Python 仍是唯一生产入口。

已实现：原生服务正常启停、API 版本/能力查询、基线项搜索与详情、资源大小写核对及真实模板预览。
未实现：设备连接、游戏点击、任务执行、Maa 生命周期、配置导入、流程编辑和生产替换。
尚未启用的工作包目录只放职责说明，不提供伪实现，不进入构建。

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

M0 的 `RESOURCE_UNRESOLVED`、真实 NEXT/Pause 与原生阻塞取消边界仍保留；
M1 完成不消除这些限制，也不授权生产切换。结束本阶段后停在 M2 之前。
