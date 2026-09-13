# M1 验收报告

日期：2026-09-13。范围：原工作包 M1，独立工程、Windows 服务启停、接口版本查询与完整基线盘点。
用户要求架构/业务分层与中文注释，本轮按职责分文件，未将 Factory 转写成大型 C++ 类。

**结论：M1 限定范围完成，停在 M2 之前；不放行生产替换。**

## 实际验收

| 断言 | 本机结果 | 证据 |
| --- | --- | --- |
| 独立 C++20/VS 2022 x64 构建 | PASS | `tools/build.py` 返回 0，生成 build/Release/automationd.exe |
| Vue/TypeScript 构建 | PASS | vue-tsc 与 Vite 7.3.6 返回 0 |
| API v1 与阶段信息可查询 | PASS | 真实 HTTP 返回 automationd / 0.1.0 / api_version=1 / M1 |
| 服务正常停止及重新启动 | PASS | Ctrl+C 后 STOPPED、exit=0；含 3 次带未完成读取的停止/重新启动 |
| 无设备/任务/写入 API | PASS | 真实写请求 405，未知设备接口 404；能力查询明确 false |
| Host/Origin、路径边界 | PASS | 非同源拒绝，穿越/非法路径拒绝，HEAD 与静态内容正确 |
| 占用端口、错误参数、中文目录 | PASS | 不接管占用端口；错误 exit=1；中文临时目录正常托管 |
| 全部基线项有去向 | PASS | 250 函数、33 配置、58 任务、82 入口；完整分支和字段详见迁移清单 |
| 专项任务/消息分发未漏 | PASS | 每个 match/case 与 AST 核对，15 个 quest 任务都找到原分发分支 |
| 资源大小写报告 | PASS（报告完成） | 8 处不符、44 处动态待验；不是宣布这些引用已修复 |
| 服务产物与索引一致 | PASS | HTTP 返回的 JSON 与生成清单逐字节一致 |
| 界面工作流与响应式 | PASS | 1440x1000、390x844：搜索、详情、过滤、原模板预览、失败提示及恢复 |
| 实际 PNG 渲染 | PASS | 原 NEXT 模板成功解码；canvas 像素含 >10 种颜色，非空白 |
| 生产代码与构建约定不变 | PASS | 5 个 Python 文件及原 bat/依赖/说明的 Git blob 与固定基线一致 |
| npm 依赖公告检查 | PASS（检查当时） | `npm audit --json` exit=0，生产/开发依赖共 0 条已知漏洞 |

`tools/validate.py` 完整执行成功：18 个 Python unittest 测试方法（9 盘点 + 9 原生服务），
2 个 Playwright 场景（桌面/移动，各含多项断言）。这些数量不是 M0 的 42 项原生断言或 18 项汇总器自检。
原生/浏览器正例使用真实编译后的服务。只有“连接失败提示”用浏览器网络拦截模拟不可达，
不以此代替 API 和正常生命周期验证。所有测试服务退出后无 automationd 测试进程遗留。

## 证据与重现

从仓库根目录：

```powershell
.venv-build/Scripts/python.exe next/tools/build.py
.venv-build/Scripts/python.exe next/tools/validate.py
```

前提与首次 Chromium 安装见 `../README.md`。全量日志位于 `next/.local/logs/`：
dependencies、inventory、npm-ci、web-build、native-configure、native-build、native-inventory-tests、browser-tests。
浏览器结果 JSON 和 baseline/case-report/asset-preview 截图位于 `next/web/test-results/`。
截图已经目视检查，页面无横向溢出，详情区域未与表格/按钮重叠。日志/截图不作为源码提交。
固定版本、依赖摘要和基线内容 hash 随源码报告保留；没有把本机用户目录或模拟器地址写入公开清单。

本轮曾修正的验收问题：Windows checkout 换行导致原字节误报、清单生成后静态镜像未重建、
初始控件/模式分发覆盖不足，以及预览 exe 运行时无法重新链接。最终验收在修正后完整重跑，
不是跳过失败用例。细节归并到项目 `docs/execution-notes.md`。

## 未完成与安全边界

- `RESOURCE_UNRESOLVED` 保留，没有重新跑 M0 内存测试或改写原失败结果。
- OCR Hit/NoHit/Error 尚待 M2 正式识别适配；没有把合成 OCR 成功当作三态契约通过。
- 真实 NEXT、Pause、原生阻塞等待取消、长期 WVD 任务与 Spark 仍未验。
- M1 不链接/加载 Maa，不连接 ADB/MuMu，不启动 Farm，不打开/关闭游戏或 VPN。
- 没有读取或改写权威 config/mod，没有打包/替换旧 dist/wvd，没有自动更新或启动器切换。
- 所有迁移项仍是 MAPPED_NOT_IMPLEMENTED；目录职责说明不是可用业务实现。
- 验收完成后，用户另行授权提交并推送个人 fork；提交状态以 Git 历史为准。此授权不包括推进 M2 或生产切换。
