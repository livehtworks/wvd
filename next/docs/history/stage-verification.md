# 早期阶段验证摘要

最后整理：2026-10-31。M1–M4 为旧 Maa 阶段；以下原生切换之前的实现、数量、身份和未验结论均有历史时点，不能作为当前启动或发布依据。

| 阶段 | 保留结论 | 当前承接 |
| --- | --- | --- |
| M1 配置与目录 | 验证配置、任务目录、持久化及错误拒绝；仅对应当时实现。 | ProfileStore、WorkflowRepository；现行数据职责。 |
| M2 执行与识别 | 执行核心、资源完整性、模板/OCR 和离线合同按原报告验收；离线通过不证明实机。 | FlowProgram、FlowExecutor、RecognitionService。 |
| M3 设备与封存 | 设备通道、快照完整性、负例、锁及停止生命周期分别验证。 | DeviceSession、BundleLease、InputGate；旧 Maa 设备代码不参与构建。 |
| M4 任务组合 | 实现过公开任务组合、补给、地图、战斗和事件规则；当轮收口仍为 PARTIAL，不补写完整游戏验收。 | 任务范围以 public-task-scope.md 和当前核心合同为准。 |
| Maa 退出 | 旧架构、数据职责、依赖、业务及 README 只保留历史；当前产品统一原生链。 | [当前架构](../architecture.md)、[核心合同](../core-contracts.md)。 |

这些是历史行为与验证的概括，既不退役当前功能，也不删除当前测试。原日志、命令、断言和失败范围按固定提交中的原报告追溯；无需在当前目录保存完整副本。

## 原报告索引

原文固定在个人仓库提交 `f4bc8f9e0d94035d1ed906516b7e37c30378581c`。需要追查某个行为时才打开对应报告。

| 原文件 | 历史标题 |
| --- | --- |
| [README-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/archive/README-maa-20260924.md) | WVD Next · Windows 自动化工作台 |
| [architecture-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/archive/architecture-maa-20260924.md) | 当前架构与业务分层 |
| [data-authority-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/archive/data-authority-maa-20260924.md) | 数据与运行文件权威 |
| [dependencies-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/archive/dependencies-maa-20260924.md) | 固定依赖与来源 |
| [wvd-business-maa-20260924.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/archive/wvd-business-maa-20260924.md) | WVD 业务层（M3 / M4） |
| [m1-fix-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m1-fix-validation.md) | WVD · M1 收尾验收报告 |
| [m1-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m1-validation.md) | M1 验收报告 |
| [m2-core-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m2-core-validation.md) | M2 运行核心与离线验收 |
| [m2-fix-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m2-fix-validation.md) | M2 修正与 Gate A |
| [m2-recognition-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m2-recognition-validation.md) | M2 离线识别单元：实现与验证 |
| [m3-device-vision-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m3-device-vision-validation.md) | M2 收尾与 M3 设备视觉验收 |
| [m3-directory-template-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m3-directory-template-validation.md) | M3 目录模板封存 |
| [m3-fix-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m3-fix-validation.md) | M3 修复与完整性成本验证 |
| [m3-integrity-entry-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m3-integrity-entry-validation.md) | M3 资源校验入口补验 |
| [m3-integrity-lifetime-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m3-integrity-lifetime-validation.md) | M3 资源锁与停止生命周期补验 |
| [m3-integrity-negative-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m3-integrity-negative-validation.md) | M3 资源封存负例补验 |
| [m4-auto-map-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-auto-map-validation.md) | M4 自动移动后的地图确认 |
| [m4-auto-wait-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-auto-wait-validation.md) | M4 自动战斗持续等待 |
| [m4-bounty-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-bounty-validation.md) | M4 悬赏揭榜与交付 |
| [m4-bull-cave-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-bull-cave-validation.md) | M4 牛洞与因果调整 |
| [m4-business-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-business-validation.md) | M4 部分实现与验证边界 |
| [m4-cave-of-separation-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-cave-of-separation-validation.md) | CaveOfSeperation 源码扩展：实现与集成交接 |
| [m4-chest-selection-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-chest-selection-validation.md) | M4 普通开箱与候选池 |
| [m4-cold-start-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-cold-start-validation.md) | M4 首次连接失败恢复 |
| [m4-combat-turn-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-combat-turn-validation.md) | M4 单角色战斗动作验证 |
| [m4-common-screen-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-common-screen-validation.md) | M4 普通阻塞画面 |
| [m4-confirmation-freshness-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-confirmation-freshness-validation.md) | M4 业务确认的帧有效性 |
| [m4-confirmation-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-confirmation-validation.md) | M4 业务确认与宝箱有限段 |
| [m4-dark-light-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-dark-light-validation.md) | M4 暗灯与遭遇超时 |
| [m4-default-dialogue-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-default-dialogue-validation.md) | M4 默认对话处理 |
| [m4-departure-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-departure-validation.md) | M4 回城补给与住宿回执 |
| [m4-dungeon-route-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-dungeon-route-validation.md) | M4 地下城路线组合 |
| [m4-dungeon-task-matrix-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-dungeon-task-matrix-validation.md) | 43 个 Dungeon 实际 TaskID 离线矩阵 |
| [m4-effect-interruption-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-effect-interruption-validation.md) | M4 副作用确认窗口 |
| [m4-encounter-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-encounter-validation.md) | M4 有限遭遇战与重复确认 |
| [m4-entry-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-entry-validation.md) | M4 入本与自动寻路验证 |
| [m4-featured-request-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-featured-request-validation.md) | M4 指定任务领取 |
| [m4-fishing-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-fishing-validation.md) | M4 钓鱼接续验证 |
| [m4-fordraig-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-fordraig-validation.md) | Fordraig 离线实现与验证交接 |
| [m4-fortress-trap-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-fortress-trap-validation.md) | M4 要塞八层陷阱专项 |
| [m4-giant-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-giant-validation.md) | M4 巨人专项有限循环 |
| [m4-global-prompt-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-global-prompt-validation.md) | M4 沙人恢复与祝福提示 |
| [m4-gold-income-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-gold-income-validation.md) | M4 7000G 剧情链 |
| [m4-golden-chest-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-golden-chest-validation.md) | M4 忍洞金箱 |
| [m4-healing-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-healing-validation.md) | M4 地下城角色恢复 |
| [m4-image-import-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-image-import-validation.md) | M4 图片扩展导入 |
| [m4-inn-payment-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-inn-payment-validation.md) | M4 住宿付款确认窗口 |
| [m4-interruption-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-interruption-validation.md) | M4 子流程普通中断 |
| [m4-iteration-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-iteration-validation.md) | M4 正常 Farm 迭代 |
| [m4-jier-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-jier-validation.md) | M4 吉尔悬赏与专用对话 |
| [m4-karma-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-karma-validation.md) | M4 善恶选择与新版配置写回 |
| [m4-lifecycle-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-lifecycle-validation.md) | M4 类型化恢复与启动就绪 |
| [m4-manual-separation-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-manual-separation-validation.md) | M4 半自动大恶魔专项 |
| [m4-mining-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-mining-validation.md) | M4 挖矿有限链 |
| [m4-native-action-timing-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-native-action-timing-validation.md) | M4 原生输入时序 |
| [m4-native-child-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-native-child-validation.md) | M4 原生子流程组合 |
| [m4-navigation-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-navigation-validation.md) | M4 地图目标与子流程组合 |
| [m4-parent-budget-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-parent-budget-validation.md) | M4 父流程有限预算 |
| [m4-party-death-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-party-death-validation.md) | M4 队友死亡提示 |
| [m4-party-defeat-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-party-defeat-validation.md) | M4 多人死亡提示 |
| [m4-pause-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-pause-validation.md) | M4 Pause 有界恢复 |
| [m4-plan-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-plan-validation.md) | M4 任务类型化数据验证 |
| [m4-png-diagnostics-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-png-diagnostics-validation.md) | M4.8 PNG诊断接线 |
| [m4-profile-boundaries-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-profile-boundaries-validation.md) | M4 配置与保存边界补验 |
| [m4-quick-chest-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-quick-chest-validation.md) | M4 快速开箱链 |
| [m4-repel-forces-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-repel-forces-validation.md) | M4 击退敌势力源码扩展 |
| [m4-revival-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-revival-validation.md) | M4 复活与失败遭遇 |
| [m4-sandman-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-sandman-validation.md) | M4 沙人缘 |
| [m4-scorpion-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-scorpion-validation.md) | M4 蝎女与六手悬赏 |
| [m4-sleep-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-sleep-validation.md) | M4 连续住宿 |
| [m4-stage-closeout.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-stage-closeout.md) | M4 本轮收口与剩余工作 |
| [m4-stage-publication-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-stage-publication-validation.md) | 分阶段图发布与续段 |
| [m4-state-routing-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-state-routing-validation.md) | M4 普通插入与状态分派验证 |
| [m4-state-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-state-validation.md) | M4 运行状态与有限段验证 |
| [m4-steel-trial-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-steel-trial-validation.md) | M4 钢试炼源码扩展 |
| [m4-stop-boundary-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-stop-boundary-validation.md) | M4 状态测试的停止边界 |
| [m4-task-handoff-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-task-handoff-validation.md) | M4.6 任务转交与长等待交接 |
| [m4-time-leap-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-time-leap-validation.md) | M4 无因果调整的时间跳跃组件 |
| [m4-travel-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-travel-validation.md) | M4 世界地图往返与组队住宿 |
| [m4-unknown-window-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-unknown-window-validation.md) | M4 未知画面窗口 |
| [m4-wall-bypass-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-wall-bypass-validation.md) | M4 重启后防空气墙动作 |
| [m4-workflow-validation.md](https://github.com/livehtworks/wvd/blob/f4bc8f9e0d94035d1ed906516b7e37c30378581c/next/docs/m4-workflow-validation.md) | M4 有限业务流程验证 |
