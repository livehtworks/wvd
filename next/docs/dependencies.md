# 固定依赖与来源

| 用途 | 固定版本 | 锁定方式 |
| --- | --- | --- |
| HTTP/异步 I/O | Boost 1.90.0 | 官方 tar.gz SHA256 + 编译版本断言 |
| JSON | nlohmann/json 3.11.3 | 官方单头文件 SHA256，CMake 再次核验 |
| Vue | 3.5.30 | 精确版本 + package-lock integrity |
| 图标 | @lucide/vue 1.45.0 | 精确版本 + package-lock |
| Vite / 插件 | 7.3.6 / 6.0.4 | 精确版本 + package-lock |
| esbuild | 0.28.1 | overrides 精确版本，处于 Vite 允许范围 |
| TypeScript / vue-tsc | 5.9.3 / 3.2.5 | 精确版本 + package-lock |
| Playwright | 1.58.2 | 精确版本及其 Chromium revision |
| Prettier | 3.6.2 | 开发依赖精确版本 |
| MaaFramework | 5.13.0 | 延续 M0 commit/SDK 摘要；M1 不下载或加载 |
| OCR 模型 | M0 固定 commit | M1 不下载或加载 |

Node 24.14.1、npm 11.11.0；Windows VS 2022 x64、C++20、CMake >=3.24。
只读工具使用标准库 Python。Black 25.1.0 仅用于隔离工具环境格式化，不是发布运行依赖。

初选 Vite 7.3.1 的 npm audit 存在开发服务公告，已采用
[Vite 7.3.6](https://github.com/vitejs/vite/releases/tag/v7.3.6) 与
[esbuild 0.28.1](https://github.com/evanw/esbuild/releases/tag/v0.28.1) 的修复版本，
不以“只是开发依赖”为理由忽略。最终 audit 结论见验收报告，不代表永远没有新漏洞。

Boost 遵循 Boost Software License 1.0，下载时保留 LICENSE_1_0.txt；JSON 单头文件保留 MIT
许可证声明。Web 各包的许可证保留于锁定安装包，发布阶段还需生成完整第三方声明。
WVD 源码和模板来自仓库固定基线，沿用根目录 LICENSE/FORK_NOTICE.md，不声明这些原始资源为本轮创作。
本轮不分发游戏、SDK、OCR 模型或生产安装包。
