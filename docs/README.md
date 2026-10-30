# 项目文档

当前说明与历史证据分开维护；接手不需要遍历报告和截图。

| 需要了解的内容 | 唯一入口 |
| --- | --- |
| 当前部署、暂停状态、批次账目和未结问题 | [项目状态](project-status.md) |
| 维护工作、处置规则和剩余事项 | [维护入口](maintenance.md) |
| 当前架构和职责 | [原生架构](../next/docs/architecture.md) |
| 输入、恢复、编译、资源及运行合同 | [核心合同](../next/docs/core-contracts.md) |
| 正式数据、用户配置与诊断数据 | [数据职责](data-authority.md)及[字段说明](../next/docs/data-authority.md) |
| 构建、工具、路径与已确认环境问题 | [执行注意项](execution-notes.md) |
| 资料/素材保留及最近核对结果 | [资料与证据管理](material-retention.md) |
| 旧阶段概括 | [阶段验证摘要](../next/docs/history/stage-verification.md)、[工程历史摘要](history/engineering-summary.md) |

## 目录职责

- `docs/reviews/incidents/`：T0 和尚需追踪的故障原报告。
- `docs/reviews/runtime/`：尚在使用的运行专项证据；问题结清时合并结论，原文回到 Git。
- `docs/reviews/engineering/`：当前工程治理与环境迁移证据。
- `docs/reviews/evidence/` 及现有审计子目录：被具体未结问题引用的证据，按该问题收尾。
- `docs/history/`、`next/docs/history/`：按主题更新的少量摘要，不创建每轮一套历史副本。
- `next/docs/`：当前技术文档；任务代码与真实测试决定行为，旧报告不是运行许可。
- `next/tests/fixtures/`：当前回归测试输入。只有调用关系和行为承接核对后才能退役。
- `next/.local/`：项目内工具缓存、隔离验证和必要本地证据；不能据目录名称认定可删。

不再按每次修复新增完整工作包、全量状态快照或截图合集。具体保留规则见资料管理页；Git 已保存的原文不复制一份到本地归档。
