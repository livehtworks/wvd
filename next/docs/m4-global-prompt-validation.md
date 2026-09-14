# M4 沙人恢复与祝福提示

## 承接范围

固定旧 `IdentifyState` 在未知状态中处理 `sandman_recover` 与 `blessing`。前者点击后重新判断场景，不调用复活计数；后者检测到 `combatClose` 时先关闭二次确认，否则点击祝福本身。

新增 `recovery/global_prompt` 是有限 Maa 子图，不持有设备、不另写调度循环。每次动作验证提示和目标，后置必须是已知页；六次提示不变时以明确原因退出。沙人/祝福属于 `blocking_screen`，已进入既有普通中断和启动分派，返回时重新观察原任务。没有继承旧全局无条件 Back/角落连点。

此批不包含任意对话选项、善恶写回、诅咒之轮等待/任务移交和全部专项。

## 验证

七目标构建通过。原生流程 7 方法/11 场景全部通过（130.091 秒，`m4-global-prompt-workflow.log`，`m4-workflow-ipqt3g7h`），覆盖沙人到 Retry、祝福普通/二次确认及中途出现确认、连续六次无效、停止/拒绝、单独关闭图标负例、缺资源提前拒绝以及回到原任务点。测试只生成私有图像和设备状态，不连接真实游戏。

实现检查点 `f009420`，EXE SHA256 `ffe7e9143ffd8fd8e90518f1f980fad93f2c08fdc27e7a5a5525fbe9102a5a7d`。同产物通用/Pause/单人和多人死亡回归 22 方法全部通过（566.347 秒，`m4-global-prompt-common-regression.log`）；不包含后续空气墙阶段改动。

首轮计划组 6 方法中 5 项通过，43 条完整迭代图的批量编译超过测试驱动的 30 秒看护时间，被判 ERROR；证据 `m4-global-prompt-plan.log`、`m4-plan-ilnirrsc` 保留。仅给该编译/序列化批次设 120 秒外部看护，逐项字段、资源、节点及零设备操作断言全部保留；不改变生产 Session、停止或输入预算。修后 6 方法全部通过（77.496 秒，`m4-global-prompt-plan-retry.log`，`m4-plan-rwdg3pjh`）；43 项仍只是编译检查，不把超时轮次算通过。
