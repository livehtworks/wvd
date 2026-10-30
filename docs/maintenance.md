# 维护入口

更新：2026-10-31。本页滚动维护剩余事项；已完成过程回到 Git 和[历史摘要](history/engineering-summary.md)，不逐轮追加工作包。

## 当前剩余事项

| 优先级 / 事项 | 已知状态 | 继续条件与证据 |
| --- | --- | --- |
| P0 存储生命周期 T0 / 正式发布历史 | OPEN；共享读取已部署，正式旧历史转换 0；本轮离线核验完成 | 原39轮成功前缀、根请求及单个候选599资源SHA通过；正式全局资格、独占租约、转换与显式回滚尚未执行。按下方准备范围进入另行授权的维护窗口。 |
| P1 MuMu 渲染崩溃、资源和内存归因 | 未全部归因，本轮未扩展诊断 | 只复用具体身份/窗口的必要证据；不能凭dump、恢复成功或某次游戏成功下结论。 |
| 历史恢复限制 | 17处修改可恢复，整棵旧工作区不可保证恢复；缺失范围已明确 | 49份原始版本、两patch和原Git索引已校验；6份Git管理文件封存后，失效worktree登记已退出。未登记/忽略材料没有恢复证明，不能宣称整树可恢复，也不阻塞现行功能承接与主分支整合。 |

200轮业务目标按用户要求排除本轮推进，保持原暂停状态。

## T0 离线核验与转换准备

- 本轮未执行正式维护CLI。`PublicationMaintenance::execute`的plan/status同样构造`ServiceInstance`，会取得并创建/删除`service.lock`；不能把它当作无写入的线上查询。
- 维护入口、service锁、storage、Windows租约和任务发布代码与已有隔离验收基线`2af5cc0`一致；复用原真实CLI的根轮/内部轮转换、finish重核、显式回滚和停止轮拒绝回执。当前产品输入仍为`fc734057…3eb72c`/1354、EXE为`15670137…1fa75`；未重新构建，未把旧EXE验收说成当前EXE新跑通过。
- 只读重新核原请求`giant-200-t0-20261025-b0ad41332a754a33b4d900f14f3b2424`的1–39轮：78份run/result及根请求原始SHA保持、三业务单元及清账条件通过，第40轮UserStopped排除；这是历史转换准备，不恢复该业务目标。
- 首个候选仍限定第39轮`repeat-58ab3883665bd519fdc6fc126c9a0fea95c25c619e0e0e09778112d2c95efb6b`。599资源SHA通过，flow/identity原件另核SHA；合计144,935,810逻辑字节。未复制完整包、未扫描全部历史、未将逻辑量当作可释放空间。
- 后续维护窗口先核最新配置、实际服务身份/静止、所有请求全局资格、前缀之外是否有重复运行引用及租约退出；沿原入口取得锁后执行原生plan。新增写入上限512MiB并保留卷2GiB余量，按真实共享集合及事务最坏增量准入，超限停止，不扩大批量。
- 首份须完成convert、读取核验、显式rollback及原目录身份/原始回执核验，才可讨论继续转换。旧完整树进入`D:/trash/wvd-publication-history/<data-root身份>/`受保护恢复命名空间；它不是普通可清空垃圾。恢复旧EXE前先显式回滚不兼容布局，禁止让旧EXE直接读取新布局。
- 当前未证明全局准入、真实独占租约及现行EXE正式转换/回滚，T0不关闭。证据复用`next/.local/consolidation-261026/history-repeat-cli-smoke-20261027-02/receipt.json`；本轮小结原位写入既有`next/.local/docs-consolidation-20261031/summary.json`，不另立报告。

## 已完成的工程结论

- 最新修复及两轮资料收束已快进合回个人master；本轮恢复核对和转换准备随同交付，提交与推送结果以Git为准。线上CI退役，构建/验收留在本地。
- 工具子进程 TEMP、缓存、.NET 输出及头文件缓存已纳入项目路径管理；旧 AppData WvdNext 和已认领构建夹具已按精确清单移至对应盘 trash。
- MuMu 迁至 F，原三实例与新实例默认落盘已验证，正式设备路径通过原配置入口更新。没有改变模拟器版本和实例资源设置；迁移后按用户要求停止。
- 47份旧巡检材料已完成逐项处置，只保留10个必要证据文件，不再作为待确认事项；旧报告收束、重复素材处置及链接检查以[资料管理](material-retention.md)为当前记录；不为这轮再创建一份历史工作包。

## 必要证据入口

- [T0 存储事故](reviews/incidents/t0-storage-lifecycle-20261026.md)、[硬链接事故](reviews/incidents/t0-bundle-link-exhaustion-20261025.md)。
- [工程收束执行](reviews/engineering/consolidation-execution-20261026.md)、[后续核对](reviews/engineering/consolidation-followup-20261027.md)。
- [工具路径整改](reviews/engineering/tool-output-paths-20261028.md)、[MuMu 迁移](reviews/engineering/mumu-f-migration-20261030.md)。
- 原 C07/C09 设计及当轮计划见[历史摘要](history/engineering-summary.md)的固定 Git 入口；旧计划的待实施措辞不再表示当前部署状态。

## 执行边界

维护文档和清理已认领的可重建材料不授予启动游戏、循环、监控、部署或推送权限。正式 `requests/runs/published`、配置、用户资产、当前及必要回滚 EXE/PDB 保持保护。未知项停止该项处置，继续其余已证实项；不增加清理守护器或另一套数据权威。
