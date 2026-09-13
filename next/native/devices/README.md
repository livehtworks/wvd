# 设备与输入层（M2 / M3）

M2 已有 `DeviceBackend` 契约与 `InputGate`，真实设备在 connect 前拒绝，具体离线后端仅在测试。
门禁验证完整帧身份、场景、应用、时效、许可和位置，并串行映射/发送输入。
记录 attempted/accepted/rejected/backend_called；停止时只允许真实释放此前按下的键/触点。
后续由设备 profile 确定唯一真实目标，不在门禁里自动扫描或选择设备。
ADB、MuMu IPC 的能力与故障必须明确记录，不能全局重启 ADB 或误操作另一实例。
设备层处理连接与触摸，不判断战斗是否完成；游戏恢复顺序由 WVD 恢复层请求。
MuMu 特有的进程操作只能下沉到 Windows 平台层。
