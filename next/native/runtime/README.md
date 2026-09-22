# 运行协调层（M2）

M2 显式离线构建已实现本层。`RunCoordinator` 独占运行、去重、租约、监督与恢复决策；
`ExecutionSession` 拥有一次有限业务单元的 Gateway 及终态，不复制 Pipeline 节点调度。
`GuardedAction` 只组合截图、场景/目标三态识别、单次许可与一次实际输入提交；
发布器为每个输入派生唯一的 `AwaitTransition` 节点，由它在独立有限预算内观察页面结果。
输入回执、页面转场和业务终态分别保存，未知转场不会重放输入。
Maa 状态与业务终态分别保存。必须正常等待在途动作和回调结束，再释放资源。
停止超时保留对象和设备占用，不伪造 UserStopped；详见 `../../docs/architecture.md`。
这里不得出现 WVD 的技能、地图、宝箱判断，也不得读取 Tk 状态。
