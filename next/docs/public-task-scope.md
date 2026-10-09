# 公开任务承接范围

目录权威：`packs/wvd/parameters/legacy-quests.json`；本轮只读重算59项，43 dungeon +16 quest，未删/禁用任何目录项。类型/ID分派权威：`native/games/wvd/tasks/run_builder.cpp::build_task_workflow()`，全部经同一Application/Coordinator/Session链。

| 目录ID/类别 | 编排承接 |
| --- | --- |
| 全部43个_TYPE=dungeon目录项 | WvdTaskPlan::parse → dungeon_iteration |
| Scorpionesses、Scorpionesses_plus_6_hands、jier、GiantBounty | 公开悬赏库组合，需BountyBuilder；没有库明确拒绝 |
| fishing、fishing2 | fishing_cycle |
| SSC-goldenchest | golden_chest_cycle |
| sandman | sandman_cycle |
| 7000G | gold_income_cycle |
| LBC-oneGorgon | bull_cave_cycle |
| steeltrail | steel_trial_cycle；代码支持但当前59项中未公开 |
| repelEnemyForces | repel_forces_cycle；代码支持但当前59项中未公开 |
| lovesleep | sleep_visits |
| manualSepDemon | manual_separation |
| FFXI-Org | mining_iteration |
| darkLight | dark_light |
| gaintKiller | giant_iteration；保留现有ID拼写 |
| fortress-B8F_trap | fortress_trap_iteration |

Fordraig/Cave工厂属于编入源码但未公开入口的休眠实现，不迁入公开菜单、不擅自退役。steeltrail/repelEnemyForces同样只记录代码支持，不伪造目录能力。

源码承接不等于每任务实机通过；实际启动仍要验证配置、语言依赖闭包、模型和冻结资源。缺繁中随机迷宫事件/疗效证据的实际调用链不放行，没有使用这些事件的悬赏不因此假造“领取悬赏”或扩大依赖。
