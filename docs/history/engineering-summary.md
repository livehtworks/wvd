# 工程与运行历史摘要

最后整理：2026-10-31。此页概括已进入版本历史的旧报告，当前状态只由[项目状态](../project-status.md)维护。历史报告内的 PID、候选、授权、通过数和限制均属于当时窗口。

## 长期有效的结论

- 产品已从旧 Maa/Python 路径统一到 C++20 原生执行链；作者流程、原生任务及异常事件共用同一执行器和数据权威。旧阶段的 NOT_RUN/PARTIAL 不能套用到今天，也不能反向改成全通过。
- 输入送达与业务成功分开；未知送达、付款和提交副作用保留原回执，观察与恢复不得重放业务输入。停止、静止和结果保存分别核对。
- 识别使用明确语言、冻结配方和完整素材；OCR/模板沿同一服务，不凭通用关闭按钮、旧帧或动画判断页面。当前完整规则在[架构](../../next/docs/architecture.md)和[核心合同](../../next/docs/core-contracts.md)。
- 路线、奖励、救人、再起、选人、技能顺序/次数、任务点和同 unit 账目按各自原业务确认推进；早期单条菜单或单轮通过不是全任务验收。
- 早期诊断开关、释放边界和性能数字只适用于对应二进制。当前未解决的渲染崩溃、内存归因及正式发布历史治理继续在项目状态和现存事故报告中单列。
- 2026-10-26 工程收束已经执行过同盘 trash 迁移，但正式历史转换仍为零、T0 未关闭。旧 C07/C09 设计与计划原文回到 Git；后续工作由[维护入口](../maintenance.md)维护，不再按每轮创建一套工作包。
- 旧批次包括 40/200、70 份及 22 份独立根、39/200，均不能与 2026-10-30 新目标合并。当前新目标为 24/200、剩 176、暂停；旧完整状态快照不再作为接手入口。

## 原报告索引

原文固定在个人仓库提交 `f4bc8f9e0d94035d1ed906516b7e37c30378581c`。表只保留查找入口，不复制过程日志、截图或完整快照。

| 原文件 | 历史标题 |
| --- | --- |
| [action-timing-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/action-timing-20260927.md) | 动作耗时记录 |
| [bounty-board-readiness-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/bounty-board-readiness-20260927.md) | 悬赏页误放行与返回链修复 |
| [city-stage-timing-20261002.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/city-stage-timing-20261002.md) | 王城阶段停顿排查 |
| [closure-ddc3366-result-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/closure-ddc3366-result-20260927.md) | 稳定性与工作台收束结果 |
| [combat-density-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/combat-density-20261004.md) | 战斗列表紧凑布局 |
| [combat-diagnostics-closure-20261002.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/combat-diagnostics-closure-20261002.md) | 单角色自动战斗与诊断补齐 |
| [combat-editor-debug-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/combat-editor-debug-20261004.md) | 战斗编辑与调试 |
| [context-recovery-20261001.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/context-recovery-20261001.md) | 游戏前台与退出恢复整改 |
| [critical-2e17e6e-result-20261002.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/critical-2e17e6e-result-20261002.md) | 2e17e6e关键修复合并结果 |
| [death-transition-20261006.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/death-transition-20261006.md) | 再起前死亡过渡页导致循环中止 |
| [flow-check-dispatch-20260926.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/flow-check-dispatch-20260926.md) | 流程检查分组与异常分派 |
| [flow-classification-repair-20260926.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/flow-classification-repair-20260926.md) | 流程分类工作包执行状态 |
| [giant-bounty-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/giant-bounty-20261004.md) | 第三章巨人悬赏 |
| [giant-resource-recovery-20261005.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/giant-resource-recovery-20261005.md) | 巨人第4轮停止调查 |
| [giant-route-completion-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/giant-route-completion-20261004.md) | 巨人战后进度与黑色导航按钮 |
| [inn-recovery-20260930.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/inn-recovery-20260930.md) | 住宿恢复与完整链路复核 |
| [key-repairs-review-20260926.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/key-repairs-review-20260926.md) | c1666a0 关键修复工作包收口 |
| [linkage-aa0957a-20261006.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/linkage-aa0957a-20261006.md) | aa0957a流程交接与内存归因交付 |
| [linkage50-deployment-20261006.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/linkage50-deployment-20261006.md) | 目标战交接修复部署与50轮采集 |
| [logging-diagnostics-20261002.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/logging-diagnostics-20261002.md) | 日志与内存诊断交付说明 |
| [managed-repeat-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/managed-repeat-20260927.md) | 连续循环控制接入 |
| [mask-portrait-transition-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/mask-portrait-transition-20261004.md) | 面具头像过渡帧与普通攻击调查 |
| [navigation-locale-materials-20261005.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/navigation-locale-materials-20261005.md) | 导航繁中素材与依赖检查现状 |
| [navigation-selection-race-20261001.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/navigation-selection-race-20261001.md) | 导航分类失效导致循环中断 |
| [party-death-dispatch-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/party-death-dispatch-20261004.md) | 死亡漩涡无动作与事件过滤 |
| [post-leap-wait-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/post-leap-wait-20261004.md) | 跳轮后重复等待修复 |
| [profile-section-saving-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/profile-section-saving-20261004.md) | 工作台分区保存 |
| [project-status-before-architecture-audit-20261010.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/project-status-before-architecture-audit-20261010.md) | 架构审计前项目状态归档 |
| [project-status-before-handoff-20261021.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/project-status-before-handoff-20261021.md) | 历史快照说明 |
| [recognition-language-20261002.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/recognition-language-20261002.md) | 识别语言隔离 |
| [recognition-methods-closure-20261002.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/recognition-methods-closure-20261002.md) | 素材识别方式闭环 |
| [repeated-boot-recovery-20261005.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/repeated-boot-recovery-20261005.md) | 第29轮恢复后停止 |
| [rescue-memory-restart-20261005.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/rescue-memory-restart-20261005.md) | 救人、启动恢复与内存诊断 |
| [roi-review-20260930.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/roi-review-20260930.md) | ROI统一排查与修正 |
| [runtime-handoff-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/runtime-handoff-20260927.md) | 运行容错审查交接 |
| [runtime-repair-review-20260926.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/runtime-repair-review-20260926.md) | 2026-09-26 原生蝎女循环修复审核交接 |
| [runtime-tolerance-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/runtime-tolerance-20260927.md) | 运行容错修复 |
| [service-lifecycle-20261001.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/service-lifecycle-20261001.md) | 后台生命周期整改：交付与部署 |
| [single-target-20261001.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/single-target-20261001.md) | 单体技能链排查与修复 |
| [strategy-frequency-20261003.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/strategy-frequency-20261003.md) | 战斗目标与频次修复 |
| [strategy-order-20261003.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/strategy-order-20261003.md) | 战斗方案复制与排序 |
| [strategy-rename-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/strategy-rename-20261004.md) | 战斗方案重命名入口 |
| [support-card-stall-20261005.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/support-card-stall-20261005.md) | 第15轮友方卡片识别停滞 |
| [support-selection-20261003.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/support-selection-20261003.md) | 友方技能选位修复 |
| [transition-convergence-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/transition-convergence-20260927.md) | 转场分支统一排查与修复 |
| [workbench-layout-20261004.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/reviews/workbench-layout-20261004.md) | 工作台紧凑布局与战斗合并 |
| [consolidation-20261026-history-contract.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/work-packages/consolidation-20261026-history-contract.md) | C09 正式历史保留与归档承接审查 |
| [consolidation-20261026-resource-contract.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/work-packages/consolidation-20261026-resource-contract.md) | C07 资源复用消费合同 |
| [consolidation-20261026.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/work-packages/consolidation-20261026.md) | WVD 工程收束落地方案与推进计划 |
| [chest-live-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/chest-live-20260924.md) | 繁中宝箱实机记录（2026-09-24） |
| [composable-workflow-local-closure-20260923.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/composable-workflow-local-closure-20260923.md) | 可组合流程工作包本地收口 |
| [composable-workflow-status.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/composable-workflow-status.md) | 可组合流程迁移状态 |
| [flow-events-local-closure-20260923.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/flow-events-local-closure-20260923.md) | 额外流程事件：本地验收记录 |
| [native-real-device-validation-53718f1-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/native-real-device-validation-53718f1-20260924.md) | WVD 53718f1 修复后的正式入口实机验收计划 |
| [native-removal-acceptance.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/native-removal-acceptance.md) | Maa 移除工作包执行记录 |
| [native-runtime-repair-53718f1-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/native-runtime-repair-53718f1-20260924.md) | WVD 原生运行链修复设计：职责、所有权与返回契约 |
| [real-device-startup-incident-20260922.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/real-device-startup-incident-20260922.md) | WVD Next 真实启动链事故报告（2026-09-22） |
| [runtime-chain-incident-audit-20260922.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/runtime-chain-incident-audit-20260922.md) | WVD Next 新旧运行链事故审计报告 |
| [runtime-chain-repair-local-closure-20260922.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/runtime-chain-repair-local-closure-20260922.md) | 运行链事故整改：本地限定收口 |
| [scorpion-combat-live-20260925.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/scorpion-combat-live-20260925.md) | 蝎女战斗实机记录（2026-09-25） |
| [scorpion-cycle-live-20260925.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/scorpion-cycle-live-20260925.md) | 蝎女实机续段：交赏、住宿与下一轮 |
| [special-combat-recognition-20260925.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/special-combat-recognition-20260925.md) | 特殊敌人战斗分流（2026-09-25） |
| [t0-repair-local-closure-20260922.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/t0-repair-local-closure-20260922.md) | T0 启动事故本地闭环报告 |
| [wheel-navigation-20260925.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/wheel-navigation-20260925.md) | 诅咒之轮矿石跳跃故障 |
| [world-map-capture-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/world-map-capture-20260924.md) | 繁中大地图素材采集（2026-09-24） |
| [execution-notes-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/archive/execution-notes-maa-20260924.md) | 执行注意项 |
| [project-status-before-observation-20260927.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/archive/project-status-before-observation-20260927.md) | 项目当前事实 |
| [project-status-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/docs/archive/project-status-maa-20260924.md) | 项目当前事实 |
