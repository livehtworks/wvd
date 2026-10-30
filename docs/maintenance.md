# 维护入口

更新：2026-10-31。本页滚动维护剩余事项；已完成过程回到 Git 和[历史摘要](history/engineering-summary.md)，不逐轮追加工作包。

## 当前剩余事项

| 事项 | 已知状态 | 继续条件与证据 |
| --- | --- | --- |
| 存储生命周期 T0 / 正式发布历史 | OPEN；共享读取与维护工具已有实现，正式旧历史转换 0 | 核原请求、完整成功前缀、静止及租约；按原入口给出有界转换与整体回滚。事故报告和最近工程报告仍保留。 |
| MuMu 渲染崩溃、资源和内存归因 | 未全部归因 | 只复用具体身份/窗口的必要证据；不能凭 dump 或某次成功下结论。 |
| 巨人目标 | 24/200，剩 176，暂停 | 只有用户新要求才核现场续同一剩余目标；不自动开批次。 |
| Git 已登记的旧工作区 | 17 处修改独立备份仍须保护；原整体 trash 登记位置曾缺失 | 未确认原件去向前不宣称整树可恢复，不改写用户修改。 |

## 已完成的工程结论

- 个人分支已完成前轮修复、素材与文档提交；线上 CI 退役，构建/验收留在本地。
- 工具子进程 TEMP、缓存、.NET 输出及头文件缓存已纳入项目路径管理；旧 AppData WvdNext 和已认领构建夹具已按精确清单移至对应盘 trash。
- MuMu 迁至 F，原三实例与新实例默认落盘已验证，正式设备路径通过原配置入口更新。没有改变模拟器版本和实例资源设置；迁移后按用户要求停止。
- 本轮旧报告收束、重复素材处置及链接检查以[资料管理](material-retention.md)为当前记录；不为这轮再创建一份历史工作包。

## 必要证据入口

- [T0 存储事故](reviews/incidents/t0-storage-lifecycle-20261026.md)、[硬链接事故](reviews/incidents/t0-bundle-link-exhaustion-20261025.md)。
- [工程收束执行](reviews/engineering/consolidation-execution-20261026.md)、[后续核对](reviews/engineering/consolidation-followup-20261027.md)。
- [工具路径整改](reviews/engineering/tool-output-paths-20261028.md)、[MuMu 迁移](reviews/engineering/mumu-f-migration-20261030.md)。
- 原 C07/C09 设计及当轮计划见[历史摘要](history/engineering-summary.md)的固定 Git 入口；旧计划的待实施措辞不再表示当前部署状态。

## 执行边界

维护文档和清理已认领的可重建材料不授予启动游戏、循环、监控、部署或推送权限。正式 `requests/runs/published`、配置、用户资产、当前及必要回滚 EXE/PDB 保持保护。未知项停止该项处置，继续其余已证实项；不增加清理守护器或另一套数据权威。
