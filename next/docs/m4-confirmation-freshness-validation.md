# M4 业务确认的帧有效性

## 问题与职责

自动地图产物 `197f531` 的输入链通过，但任务点确认曾使用约 2433.96ms 前的帧。`WvdConfirm` 原来只检查 Hit、动作资格、帧编号与代次，没有检查输入门禁的 2 秒帧龄。`WvdCombat`、`WvdChest` 在准备技能/角色前也有同类缺口，追加识别耗时可能使早先场景证据过期。

`InputGate::current_observation` 复用现有帧身份、代次、epoch、连接代次、应用与帧龄检查，仅返回证据是否仍有效，不建立输入许可。Context 暴露受限只读查询。三个 WVD 状态消费者在状态锁内、最后一次识别之后、修改状态之前重新检查；取消仍优先退出。

失败分别为 `BUSINESS_CONFIRMATION_STALE`、`COMBAT_CONFIRMATION_STALE`、`CHEST_CONFIRMATION_STALE`。不重写捕获时间、不扩大 TTL、不让旧帧计数或持久化。Hit 和动作资格仍由消费者独立检查；门禁查询不是识别成功的替代品。

地图确认的纯 `focus_cursor / reached / through_stair` 条件可参加既有顶层双路视觉求值，它们只读取当前像素与封存模板，不修改历史。此调整不允许业务条件或运动历史并发。

## 验证范围

七个目标构建通过（`m4-confirm-freshness-build.log`）。流程 4 方法/5 场景通过（45.382 秒，`m4-confirm-freshness-workflow.log`，私有根 `m4-workflow-9s_yqyhl`）：新鲜任务点正例、错误步号负例、三秒旧帧下任务点不递增、角色不准备技能、善恶不准备/写回。5 份退出码、EXE 身份、终态、输入次数和 quiescent 已核对。离线设备显式返回捕获时间，截图不推进场景。

同产物 M2 全部 102 方法通过（97.546 秒，`m4-confirm-freshness-m2.log`），原有门禁测试增加当前/过期/关闭/新帧/新代次断言。地图组合和其它 WVD 回归继续进行，不将 M2 通过代替 M4 全量通过。

`WvdChest` 消费者也已接检查，但全旧帧会先被前序 `WvdConfirm` 拒绝，不能据此声称已单独验证开箱角色回调的过期出口。必须区分实现覆盖和已执行负例。

真实设备、长期资源稳定性、整个 TaskID 验收不在本专题通过范围内。
