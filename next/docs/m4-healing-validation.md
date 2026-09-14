# M4 地下城角色恢复

## 结构与语义

`supply/dungeon_recover.cpp` 编译有限角色面板流程；`WvdRunState` 持有恢复需求、活动请求和独立请求序号。住宿和应用/模拟器恢复仍是各自职责，不复用角色恢复标志。

按照固定旧源 `StateDungeon` 的条件，在入本需要恢复、未跳过的战斗/宝箱后、复活后产生需求。入本/复活标志在开始尝试时消费；未确认恢复结束前，待恢复需求仍保留。新遭遇和新代次撤销旧面板意图，不丢失尚未满足的需求。

动作顺序保留前排三角色轮换、剧情页签分支、查找恢复页、600/1200 恢复和最多五次返回。每次输入前证明场景，战斗/宝箱/复活优先退出，不继续点击旧面板；已经回到地下城则不发送剩余返回。没有复制旧无条件 `[1,1]` 清场连点，未知覆盖层按未知失败处理，不声明完整未知页处理已迁移。

`healing_requested` 与 `healing_completed` 都使用稳定 operation ID。重复请求、迟到旧完成记录不得清除下一次恢复需求；发生实际恢复并重新确认地下城后才清除需求。纯业务条件只负责选路，确认动作另取新帧证明场景，状态所有者再次核对请求是否合法。

## 首轮问题

保留以下失败证据，不覆盖为通过：

1. `m4-healing-tests.log`，`next/.local/m4-workflow-dl615o6c/`：5 方法失败。新增摘要字段未接入 `business_condition` 显式白名单，输入前 `BUSINESS_CONDITION_FIELD_INVALID`。修正仅加入 `/healing_required`，不开放任意摘要路径。
2. `m4-healing-condition-tests.log`，`next/.local/m4-workflow-kn8wubm0/`：7 方法中 6 个失败，均在操作前 `BUSINESS_CONFIRMATION_MISSING`。纯业务条件没有视觉许可，不能与视觉一起塞入 WvdConfirm。拆为 observe 选路和独立新帧确认，不放宽 action_eligible 或 TTL。

修正上述接线后，`m4-healing-final-tests.log`（`m4-workflow-ml4e8vp9`）7 方法仍有 4 个夹具失败：三条返回链把 ActionKind=1（Swipe）误写成返回键，严格设备拒绝了实际 ClickKey=5；停止夹具在 5 秒内等不到首个输入即抛 CHECKPOINT_TIMEOUT，析构正常停止了尚在观察的 Run。修正为既有 ClickKey 枚举，并在公开 Session 预算内等待首个输入；不延长生产 stop_timeout 或图像有效期。

最终 `m4-healing-fixture-tests.log` 的 **7 方法/11 场景通过**（200.362 秒），证据 `next/.local/m4-workflow-e5p4t37p/`。建构日志 `m4-healing-fixture-build.log`。每场景记录 EXE hash、真实加载 DLL 路径/hash、实际调用数与最终业务状态。包含剧情/普通页签的完整四次输入、轮换三角色并查找恢复页的七次输入、无需恢复零输入、三种遭遇打断、拒绝/停止、五次返回耗尽和未知后置画面。

`m4_inventory.py --healing-evidence` 核对上述当前 EXE 身份、全部终态/输入次数及恢复需求后更新 StateDungeon 子范围；58 项完整任务仍未改为通过。状态直接契约首组 `m4-healing-state-tests.log` 已通过 11 方法；当前构建的必要回归另行补记，不将旧构建历史数当本次复验。

同一最终构建的 M2 回归 `m4-healing-m2-regression.log` 已通过 100 方法（110.521 秒），状态回归 `m4-healing-state-regression.log` 11 方法通过。没有重跑 M0 资源组或已达有限修正边界的 M3 发现组。

M3 默认/显式 ROI、受控 Custom 因果链、组合条件三态的 3 个方法通过（30.706 秒，`m4-healing-m3-regression.log`）；原子任务、技能和 Auto 等待停止的 3 个方法通过（11.792 秒，`m4-healing-stop-regression.log`）。所有组串行执行，450 个保护文件未变。

## 范围

此流程仍是完整地下城任务的组成部分，不把子终点当成整个任务已迁移。后续还需接完整路线、恢复调度、死亡/Pause 及专项任务；58 项完整任务分母和真实验证边界不变。
