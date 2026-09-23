# 可组合流程迁移状态

## 权威边界

- 现有游戏任务的直接入口仍是原 C++ 工厂，统一标记为 `LEGACY_NATIVE`；本轮没有切换任何任务入口。
- 12 份公共定义已进入同一 WorkflowRepository、作者编译链和可视化编辑器，状态为 `STRUCTURE_VERIFIED`。
- `AUTHOR_DEFINED` 只用于以后已经完整承接某个任务唯一入口的定义；本轮为 0。
- `REAL_SCOPE_VERIFIED` 只记录新作者图在真实设备完成的范围；本轮设备操作为 0，因此为 0。

## 公共定义

| 范围 | 定义 | 当前状态 | 边界 |
| --- | --- | --- | --- |
| 城市 | `city-enter-guild`、`city-open-ruins` | `STRUCTURE_VERIFIED` | 仅验证结构、参数、编译和编辑，不新增城市身份推断 |
| 公会 | `guild-enter-commissions`、`guild-open-bounties`、`guild-open-bounty-page` | `STRUCTURE_VERIFIED / BLOCKED_RESOURCE` | 缺当前可靠繁中委托页、赏金页证据 |
| 领取 | `guild-claim-first-bounty`、`bounty-refresh` | `STRUCTURE_VERIFIED / BLOCKED_RESOURCE` | 缺第一项与领取回执；不可逆领取仍禁止自动重试 |
| 跳轮 | `wheel-open`、`wheel-select-target`、`wheel-jump`、`wheel-reset` | `STRUCTURE_VERIFIED` | 保留目标未出现时下滑和既有等待语义，尚未用新图实机验证 |
| 王城终点 | `ore-reset-to-royal` | `STRUCTURE_VERIFIED` | 王城只由只读身份资源确认，不把公共建筑图标当城市身份 |

明确缺可靠 recipe：`guild.commissions.page`、`guild.bounties.page`、
`guild.bounty.first_item`、`guild.bounty.receipt`。缺项不会回退到英文，也不会通过放宽阈值绕过。

## 已验证契约

- 同一公共步骤可在一个调用者内以不同参数实例化，且不会修改公共定义。
- 有序插槽扩展随调用保存；空插槽不产生额外步骤；循环、深度和缺引用会显式拒绝。
- 启动时在同一仓库锁内冻结引用闭包；编译期间不回读活动文件。
- 编辑器可保存重开、打开被调用定义、返回原调用节点；被引用定义不能删除。
- 作者图最终仍编译到既有 Pipeline、RunCoordinator 和输入门禁，不新增调度器或设备旁路。

这些结论不代表公会领取、跳轮或完整任务已经通过真实设备验收。
