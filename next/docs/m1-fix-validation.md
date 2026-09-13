# WVD · M1 收尾验收报告

## 1. 执行身份和范围

- 日期：2026-09-13；工作包：WVD-M1-FIX-a03f15d。
- 个人仓库：livehtworks/wvd；分支：agent/local-stability-notes。
- 审核基线与执行前 HEAD 均为 `a03f15d0c331232b0c25334115aa793b0833252b`；初始工作区干净。
- 只修复 R01 / R02 / R03，原生产基线为 `6585f4075f5714ab522aa582993860c09af912c1`。
- 用户最新直接请求为“完成该工作包，没有问题后就继续往下推进”。这是对附件“停在 M2 前”的阶段范围修订：本报告先独立封存 M1 收尾，再允许开展原方案 M2；不包含 M3、实机操作、生产切换或新一轮 commit/push。
- **本报告所有 PASS 均只属于 M1 收尾时刻，不是 M2 已通过。** 该时刻没有加载 Maa、连接设备或修改生产文件，未 commit/push。

源码标识为上述 HEAD，加 `next/.local/m1-fix/source-tracked.diff` 及新增驱动源码。
diff 不包含本报告、历史报告的入口追加，也不包含后续阶段变更；不使用循环依赖的报告自身 hash。

| 封存对象 | SHA256 |
| --- | --- |
| 已跟踪文件源码/生成清单 diff | `5462449e342ef86436c810c8d2a8e70120797785d8e2913943c930472a91d84c` |
| 新增 tests/native/test_head_response.cpp | `bf1028e3993b1d8c576cbe94c5e3a0a27984fd8b56c3a560dc6644a21baec51a` |
| automationd.exe | `152d6e128ae58870ec3255bc57664338cc5ed2ecc8ab5e1061e19f829d19acde` |
| test_head_response.exe | `0e394bd7fba3db5d8a025f9b8f9d0b95048c31b183eb0f025855491afb750f23` |
| Web 文件清单（442 个文件，逐项内容 hash） | `465333d57751957d664482da9eba636350f54d8adcc629166d6908b7aef01d3e` |
| feature_inventory.json / Web 静态镜像 | `fc756e0ec32d44fa5ebe2180e88173cb6e199dc3ebd75d6b9062bfbbc4899155` |

二进制、Web 快照、原始日志、逐文件清单仅封存在忽略目录 `next/.local/m1-fix/`，不作为公开材料提交。

## 2. 逐项结论

| 项目 | 结果 | 实际修改 | 剩余边界 |
| --- | --- | --- | --- |
| R01 | PASS | routes 内部辅助函数 + Connection 实际发送前统一收口；新增真实 TCP 和内部 500 测试 | 内部 500 不冒充网络触发异常 |
| R02 | PASS | SCRIPT_EXACT 明确归属，从 Pause 正则移除；原生成器重新生成 | 只修正映射，未实现识别器 |
| R03 | PASS | 成功 load 后按当前选择 ID 重取对象、限制页码 | 只读数据同步，不增加编辑/任务能力 |
| 全量回归 | PASS | 23 项 unittest、4 个 Playwright 场景 | 不是 M0 或实机测试 |
| 生产保护和清理 | PASS | 450 个受保护文件 hash 相同；测试进程正常退出 | 原预览为构建正常停止，见第 7 节 |

变更文件仅为工作包白名单：CMake 新增有限测试目标；api 三文件；test_service.py 和新增原生驱动；
mapping.py / test_inventory.py；useInventory.ts / workbench.spec.ts；生成索引和两份允许的报告。
未修改提取算法、页面组件、样式、API schema、安全校验、连接上限或超时。

## 3. R01 原始字节与内部 500

`route()` 现在始终构造完整响应，`Connection` 在正常/早返回错误/catch 响应构造后恰好调用一次
`finalize_response_for_send()`。只清空 HEAD 正文，不再次 prepare_payload，不重算 GET 表示长度。

真实 TCP 检查器用 socket 读到正常 EOF 后按第一个 CRLFCRLF 分割；超时、reset、缺头部都会失败。
全部 GET 也用同一检查器，逐字节核对声明长度；错误 JSON 另核对原 error_code。

| 方式 | 请求/场景 | 状态 | Content-Length | HEAD 实际正文 B | GET 正文 B | 结果 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| TCP | 临时静态根 `/` | 200 | 18 | 0 | 18 | PASS |
| TCP | `/api/v1/version` | 200 | 72 | 0 | 72 | PASS |
| TCP | `/api/not-found` | 404 | 28 | 0 | 28 | PASS |
| TCP | `/missing.png` | 404 | 32 | 0 | 32 | PASS |
| TCP | `/%xx` | 400 | 29 | 0 | 29 | PASS |
| TCP | `/%2e%2e/secrets` | 403 | 31 | 0 | 31 | PASS |
| TCP | 非法 Host | 403 | 29 | 0 | 29 | PASS |
| TCP | 非同源 Origin | 403 | 36 | 0 | 36 | PASS |
| 内部驱动 | 受控异常 500 | 500 | 31 | 0 | 31 | PASS |
| 检查器自检 | 故意带 3 B 正文的 HEAD | 404 | 3 | 3（被拒绝） | 不适用 | PASS |

内部驱动通过实际 wvd_http 的同一辅助函数及 Beast 序列化检查输出，不依赖 Release 中可能关闭的 assert。
它由原 Python 测试入口执行，返回 0；没有新增故障路由。原带未完成请求的三次停止/重启回归继续通过。
本轮未出现读取超时、GET 被清空、长度错配或强制清理。

## 4. R02 生成差异

全部变化条目如下，没有作用域外变化；new_owner 均仍为 `native/games/wvd/vision`：

| ID | 源位置 | 原入口 / 验收族 | 新入口 / 验收族 |
| --- | --- | --- | --- |
| function:src/script.py:Factory.StateCombatCheck:2452 | src/script.py:2452 | WvdPauseRecognizer::StateCombatCheck / M3-PAUSE | WvdBattleRecognizer::StateCombatCheck / M3-VISION |
| branch:src/script.py:2460 | src/script.py:2460 | WvdPauseRecognizer::StateCombatCheck / M3-PAUSE | WvdBattleRecognizer::StateCombatCheck / M3-VISION |

直接 owner 调用和生成清单均验证了目标归属；CheckPauseOverlay、CheckPauseTextLayout、
GetPauseNegativeEvidence、TryReadPauseTextByOcr 仍属于 Pause 识别；TryResumePauseOverlay 仍属于恢复业务。
浏览器未拦截的真实清单搜索及详情显示了 WvdBattleRecognizer / M3-VISION。

计数保持：函数 250、配置 33、任务 58、任务字段 1347、入口 82、分支 784、类 24、lambda 195、
运行属性写入 337、字典字段 221、构建约定 6、资源 440。源位置、签名、函数体 hash、调用记录均与审核基线一致。
所有条目仍为 MAPPED_NOT_IMPLEMENTED；资源报告整体与审核基线 JSON 相同，8 处大小写及 44 处动态待验未隐藏。
HTTP 下载由原全量测试与生成 JSON 逐字节比较，故其 SHA256 与上表生成文件完全相同。

## 5. R03 浏览器结果

| 场景 | 1440x1000 桌面 | 390x844 移动 |
| --- | --- | --- |
| 同 ID 内容更新，列表与详情一致 | PASS | PASS |
| 41 条第 2 页缩到 1 条，回到 1/1 | PASS | PASS |
| 已选 ID 删除，详情关闭 | PASS | PASS |
| 当前筛选结果为空，筛选保留、1/1 | PASS | PASS |
| 清单更新但第 2 页仍有效，保留页码 | PASS | PASS |
| 内容不变，选择保留 | PASS | PASS |
| 请求等待期间改选，采用最近选择 | PASS | PASS |
| 原模板预览、搜索、错误提示/恢复 | PASS | PASS |
| 无页面异常及新增横向溢出 | PASS | PASS |

新增场景只拦截清单 GET，取真实 schema 中 41 条函数构造受控变更，不写服务端文件；总计 6 次预期请求，
没有纠正 watch 循环或重复 load。版本/能力/静态页仍来自真实 C++ 服务。
原成功场景不拦截清单；原连接错误分支仍只用原有版本请求网络拦截验证提示，不冒充正常生命周期。
截图已目视检查：`web/test-results/workbench-刷新同步详情、筛选及有效页码-{desktop,mobile}/`，
含 refresh-updated-detail、refresh-clamped-page、refresh-empty-filter。R02 另有 combat-ownership 截图。

## 6. 构建、回归与修正轮次

- 沿用锁定 C++20 / VS 2022、Boost 1.90.0、JSON 3.11.3、Node 24.14.1、npm 11.11.0、Vue 3.5.30、Playwright 1.58.2；依赖锁文件未改。
- `next/tools/build.py` 完整生成、npm ci、Web/CMake 构建 exit=0；新增驱动格式整理后再次按同一预设构建原生目标 exit=0。
- `next/tools/validate.py` 最终 exit=0：11 项盘点 + 12 项原生服务测试，4 个浏览器场景通过。
- R01 第一轮 12 项服务测试通过；R02 第一轮 11 项盘点测试通过；R03 随最终完整验证首次通过。没有失败后改判据、重复修正挑 PASS 或禁用用例。
- 日志：`next/.local/m1-fix/r01-round1.log`、`r02-round1.log`、`r02-generation-round1.log`、`final-build.log`、`final-validation-round1.log`、`native-final.log`、`browser-final.log`。
- 原生增量构建细节：`next/.local/logs/m1-fix-final-native-build.log`；浏览器结构化结果已封存为 `next/.local/m1-fix/browser-results.json`。
- 封存 EXE、内部测试驱动和 Web 静态快照位于 `next/.local/m1-fix/artifacts/`，防止后续阶段构建覆盖本轮证据。

## 7. 保护和阶段结论

执行前保存了生产源码/资源/语言/旧打包与依赖，以及存在的用户配置、mod 和旧 EXE 的内容 hash。
最终 450 项全部一致，保护清单仅保存在本地，不公开配置内容或私人目录。
M1 能力查询仍为 stage=M1、Maa 未加载，device_control/task_execution/websocket/production_switch 均为 false。

构建前核对了已有预览进程的完整 EXE 路径，确认属于本目录后发送 Ctrl+C；日志显示 STOPPED，进程退出。
本报告封存时预览未重启，所有本轮测试服务正常停止；因此不声称“完全未改变预览现场”。没有结束游戏或模拟器。

**M1 收尾完成。** 截至本报告封存时仍未开始 M2 实现；依据用户最新请求，可在此门槛通过后另行推进原 M2。
本轮不执行 commit/push。RESOURCE_UNRESOLVED、旧 FAIL、OCR 三态、原生阻塞取消、真实 NEXT/Pause 与 Spark 边界继续保留，
不能因 HTTP 修复和页面测试通过而销项，也不能把后续实现结果追写成此次 M1 通过。
