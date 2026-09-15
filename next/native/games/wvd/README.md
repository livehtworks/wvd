# WVD 业务层（M3 / M4）

当前 `vision/` 已实现纯视觉并经同一 MaaGateway 注册；profile、任务目录/类型化数据、
运行状态和策略消费已实现。导航、补给、技能、宝箱、恢复和十五个基础专项已有有限
Pipeline接线；多条任务有真实Maa离线子链证据，但完整任务矩阵仍是0/58，并保留明确
阻断。当前没有新版生产挂机入口；不得把 Python Factory 整体翻译成一个 C++ 类。

视觉分工：`asset_resolver` 处理来源/hash/显式别名与基线优先；`recognizers` 处理版本化模式、
模板、ROI、mask、Pause、战斗和地图纯判断；`image_ops` 保留旧通道预处理的截断语义；
`bobber` 保留方向场与去重算法。均不创建 Maa 句柄、不点击或重启。
完整盘点与验收缺口见 `next/docs/migration/m3-implementation-map.json` 及 M3 报告。

`state` 不保存图像、Controller 或坐标缓存；`combat/strategy` 只处理纯策略，不执行输入。
`tasks/task_plan` 解析旧路线的顺序操作、滑动和目标提示，输出可审查的纯数据；
不能把 parsed plan 当作可运行任务。`pipeline_compiler` 只生成有限静态图；
`workflow_session` 检查资源/权限/注册表后发布新包，执行节点只由 Maa 推进。
`navigation/world_map`、`supply/inn`、`combat/auto_combat` 保留各自业务边界，
`supply/policy` 只计算补给条件。没有新增逐节点 VM 或生产入口。
状态、任务数据和子流程范围见对应的 `next/docs/m4-*-validation.md`。

下表是职责边界，不表示表中每个业务族均已完成：

| 子职责 | 输入与结果 | 不负责 |
| --- | --- | --- |
| vision | 帧与模板/ROI -> 识别结果及证据 | 点击、恢复、策略消费 |
| combat | 当前角色、策略快照、目标观察 -> 战斗动作与业务结果 | 原生句柄和网络线程 |
| navigation | 地图/楼层/移动观察 -> 路线步骤与到达确认 | 设备重启 |
| supply | 入城、组队、住宿、补给间隔 -> 补给完成确认 | 擅自改变背包处置策略 |
| chest | 宝箱、陷阱、转战斗观察 -> 开箱业务结果 | 转场后继续旧输入 |
| fishing | 近远端参数、浮标与结果 -> 钓鱼有限步骤 | 通用运行调度 |
| recovery | 启动页/Pause/卡死证据 -> 有界恢复请求 | 直接关闭不确定身份的设备 |
| tasks | 全部任务 ID、类型、路线与特殊对话 -> 有限业务段组合 | 第二套 Pipeline 引擎 |

公共 runtime 不导入这里的具体游戏规则。每个业务段切换必须承接完整入口、字段、
终点、副作用与恢复能力；盘点清单中的验收标识用于逐段核销，不代表现在已完成。
