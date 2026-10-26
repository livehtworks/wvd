# WVD Next Windows 工作台

当前候选使用 C++20、Vue、OpenCV、ONNX Runtime、MuMu IPC、ADB 和 scrcpy 控制协议；正式服务不加载 MaaFramework。旧 Python 程序及 `next/dist/wvd-next` 均未替换。

## 入口与数据位置

新安装的默认打包目标是 `next/dist/wvd-next-native`，成功打包后其中的 `启动WVD原生版.bat` 用于打开工作台。未显式指定 data-root 时，新版配置与作者流程默认写入 `%LOCALAPPDATA%/WvdNext`；既有部署应使用其已核验的启动参数和 data-root，不以默认值覆盖现场。旧 `config.json`、`mod` 和日志不会被新版回写。首次使用请在工作台核对模拟器路径、实例编号、ADB 地址和 VPN 选项。

**默认输出位置不等于当前正式候选。** 以下是 2026-10-26 [收束盘点](../docs/reviews/consolidation-inventory-20261026.md) 的已记录现场，后续操作前以 [项目当前事实](../docs/project-status.md) 及实时身份复核为准；路径均从仓库根目录起算。

| 项目 | 盘点时状态 |
| --- | --- |
| 正式候选 | `next/.local/t0-candidate-261025/` |
| 正式 data-root | `next/.local/c11-flow-product/data` |
| 默认打包目录 | `next/dist/wvd-next-native`，交付标记仍为旧提交 `8cdb464` |
| 当前收束状态 | 候选、默认入口及回滚材料尚待统一；未因本次文档整理切换服务 |

数据职责统一见 [持久数据职责](../docs/data-authority.md)。不能仅因目录位于 `.local`、`build` 或 `dist` 就将其认定为可清理产物；当前候选、配置、回执及对应诊断符号的保护范围见 [收束计划](../docs/work-packages/consolidation-20261026.md)。

## 已核验范围

2026-10-25 T0 修复已完成构建、打包、正式部署及相应离线验收，范围见 [修复验收及发布](../docs/reviews/t0-lifecycle-repair-20261025.md)。截至 2026-10-26 盘点，新的独立 200 轮目标中，`GiantBounty/zh-Hant` 已核 run1–39 共 39 份完整成功回执，每份包含三业务单元及 `result_saved/quiescent`；run40 为 `UserStopped`、仅一业务单元，不计成功。该批次与旧批次分别记账。

这项实机证据覆盖上述任务、语言、候选和批次，不代表所有任务、所有语言或 200 轮目标已经通过。盘点时内部 repeat 已停止、监控 PAUSED；本次文档整理不续跑、不恢复监控。产物生命周期事故及旧导航等未结问题仍以 [项目当前事实](../docs/project-status.md) 和专项报告为准。

## 产品导航与构建

正式产品导航只有工作台和流程编辑。运行状态和停止入口由 App 常驻；配置分为常用参数、战斗方案、设备与高级，迁移盘点不再参与产品构建和发布。

在仓库根目录构建：

```powershell
python next/tools/build.py
```

需要 VS 2022 x64 C++/CMake、锁定的 Node/npm 版本。`build.py` 校验固定依赖 SHA256，经显式作者资源生成事务后构建 Vue 和唯一原生服务，再调用原打包入口。正式服务 EXE 的 Release 构建目录为 `next/build/Release`，默认候选输出为 `next/dist/wvd-next-native`；构建、打包、部署和实机验收分别记录。

执行前按 [执行注意项](../docs/execution-notes.md) 核本轮输出位置、增量预算及产物收尾。具体任务只运行对应的窄验收，不默认执行旧全量 `validate.py`；仅文档整理不据此重跑构建或游戏。测试必须使用独立可丢弃 data-root，不连接正式服务。`ddc3366` 当时的隔离端口、独立候选及命令保留于 [历史收束报告](../docs/reviews/closure-ddc3366-result-20260927.md)，不作为当前部署身份。离线通过不等于游戏任务通过。

## 历史资料与维护

历史迁移报告原文在 [冻结归档](docs/archive/migration-baseline-ddc3366/ARCHIVE.md)。`tools/inventory/generate.py` 仅供人工审计固定旧基线，输出到 `next/.local/audit/migration-baseline/`；不再向 web/public 写报告或复制预览图片。

当前架构与限制见 [架构](docs/architecture.md)、[工作包执行记录](docs/native-removal-acceptance.md)和 [项目状态](../docs/project-status.md)；旧 Maa 阶段资料在 `docs/archive/` 与 `archive/`，不再是构建入口。当前维护推进以 [收束计划](../docs/work-packages/consolidation-20261026.md) 为任务入口，保留历史报告的原日期、提交和验收范围。
