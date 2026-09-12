# GPT 审核入口

## 本次提交是什么

这是 2026-09-13 独立 Windows 选型验证的审核快照，固定 MaaFramework v5.13.0，WVD 对照提交为 `6585f4075f5714ab522aa582993860c09af912c1`。
生产仍然使用既有 Python/Tkinter/OpenCV/ADB/MuMu IPC 链路。本次没有迁移业务、改变生产配置、重新打包或向原作者提交 PR。

先读 [实测报告](reports/FINAL_REPORT.md)、[逐项结果](reports/case_results.json) 和 [范围修订](reports/SCOPE.md)。
32 个固定项目：28 PASS、1 FAIL、2 UNVERIFIED、1 OUT_OF_SCOPE；28 个必选项目中 27 个通过。

**工程解释：核心链路可行，长期资源稳定性尚未验收。** 报告的 NO_GO 是固定门槛未全部满足，不是框架不适用或已证实泄漏。
内存斜率约 267.09 KiB/轮，比 256 KiB/轮上限高约 11.09 KiB/轮；每轮为 100 节点的完整会话，不是总共只多 11 KiB。
保留 FAIL 原判，不通过调阈值或挑选复测结果改成 PASS。

## 建议重点审核

1. [执行与状态](probe/core/main.cpp)：Completed/UserStopped/Failed/RecoveryRequired 是否正确；停止检查点、嵌套 ContextRunTask、回调异常、旧 generation 拒绝和资源释放是否足以支撑结论。
2. [假设备](probe/adapters/fake/fake.cpp)及[状态定义](probe/core/probe.hpp)：输入记录、停止后的输入拒绝、条件变量和回调状态生命周期。
3. [实机适配器](probe/adapters/windows/device.cpp)：实际 Maa 控制器、截图方向、启动链、120 秒停止边界及王城终点判定。请注意这是验证辅助，不是已经接入生产的恢复状态机。
4. [Windows 指标采样](probe/adapters/windows/platform.cpp)和[35 轮内存记录](reports/memory_samples.json)：后段线程数 5→7 与内存上涨的关系，测量方法及潜在混杂因素；目前归因未完成。
5. [识别对照](reports/parity_results.json)与[AST 对照器](scripts/fixtures.py)：基础模板中心、ROI 和框架坐标缩放的证据是否充分。NEXT 遮罩和真实 Pause 回归仍未验证，不要从合成 OCR 推断通过。
6. [证据汇总脚本](scripts/collect.py)：逐项 PASS 是否覆盖了对应验收断言，有无把局部成功外推为整条链路通过。

第三方 `probe/core/json.hpp` 是固定 nlohmann/json 3.11.3，哈希和许可证已保留；无需将它作为本轮自写代码逐行审核。

## 证据和复现边界

- 提交了原生探针源码、构建/运行/收集脚本、依赖锁、原算法片段和许可证、合成样本、脱敏报告及数值结果。
- 不提交真实截图、完整事件/命令日志、SDK/DLL/EXE、OCR 模型、完整上游仓库、真实配置、本机路径或账号信息。原始失败日志留在本机独立验证目录。
- `reports/fixtures_index.json` 中标签来自 Codex 画面审阅，不是人工金标准；图片哈希可对应本机原证据，但 Git 中没有这些真实图片。
- `reports/cleanup.json`、环境信息和 Git 干净状态均为实测结束时的快照，不是本次整理提交后的重新测试。
- `reports/artifact_hashes.json` 对应实测交付时的原生代码和 EXE，不将后续文档整理冒充新一轮实测。
- 部分脚本用于整理本轮固定 case 目录；`[VALIDATION_ROOT]`、`[TARGET_SERIAL]` 等是脱敏占位符。缺少私有现场时不能直接重跑全部验收，更不能用缺失样本算通过。
- 不应从审核目录直接运行 `cold_start.ps1` 或清理脚本。复测应在仓库外独立目录重新确认设备所有权和授权范围。

请按“确定缺陷 / 验证缺口 / 可改进项”区分结论，并给出代码位置；不要先修改生产工程或改变验收阈值。
