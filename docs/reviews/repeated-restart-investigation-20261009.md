# 连续重启现场核查

本轮按用户要求只读核查，未修改产品、驱动或模拟器设置，未启动新批次。candidate132/PID45260，协调器EFA3C994-9B06-4E05-9F57-DA18C62D41CF。

## 已确认原因

1. 第3轮读取恢复约74147ms后，seq574已经报告application_restarted_menu_reclassified，seq577紧接着又启动CONTINUOUS_EXCEPTION_TIMEOUT恢复。FlowExecutor成功恢复实例/应用后没有重置旧exception_since_；旧异常计时包含了实例启动等待，恢复后首个tick又达到60秒，导致刚恢复的游戏再次重启。相关代码为next/native/runtime/flow_executor.cpp的retry_observation与tick。
2. 第4轮战斗后的返城Outskirts输入遇到网络错误，NetworkOverlay_RetryZhHant至少5次产生confirmed回执，但业务仍未恢复；持续异常60407ms触发游戏重启。随后实例退出并由恢复链拉起。DEVICE_INSTANCE_STARTING共16次是观察等待，不能当成16次实际重启。
3. MuMu实例2的shell.log.1在13:37:37.034明确记录error901/Graphics Driver crash，13:37:37.035记录crashModule=nvoglv64.dll、isGraphicsCrash=1，随后上报shell crash900。该次模拟器退出的直接来源是图形驱动崩溃，不能只凭WVD MUMU_INSTANCE_MISMATCH归因成程序主动关闭实例。MuMu日志路径为C:/soft/MuMu Player 12/vms/MuMuPlayer-15.0-2/logs/shell.log.1，行7742至7746；当前物理显卡RTX3080Ti，驱动32.0.15.9186。未据此安装/更换驱动。

## 随后循环停止

第4轮已Completed、quiescent/details_complete=true，悬赏提交1次、住宿1次、报告剩余0、业务周期完成1。恢复过程中foreground_lost_pixels截图保存失败：DIAGNOSTIC_PIXELS_INVALID，导致secondary_errors=[DIAGNOSTIC_INCOMPLETE]。application.cpp续轮门禁要求secondary_errors为空，因此停止REPEAT_CYCLE_NOT_CLEAN，累计计数仍为3/68；第4轮真实业务完成但未进入续轮计数。

当前工具仍在，循环inactive。此前100轮完成32轮，续批已确认4轮业务完成，续批界面只计3轮。不要把第4轮当作未提交而重做。

## 证据与边界

- 运行证据：next/.local/c11-flow-product/data/runs/EFA3C994-9B06-4E05-9F57-DA18C62D41CF/{3,4}。
- 只读接口现场快照：next/.local/restart132-review-20261009-134055。设备诊断环形缓存会淘汰、且缺动作时间戳，不据其剩余计数推导整个批次重启总数。
- 首两轮未见读取恢复；第3、4轮均见实例/应用恢复。业务crashes=0遗漏这些恢复，不能代替实际事件日志。
- 第4轮过程中工具私有提交曾约550.5MiB；后续日志154189824字节，系统提交68668715008/72900411392，约94.2%。存在高提交压力，但尚无证据证明它导致本次图形驱动崩溃，资源问题仍未闭合。
- 本轮指出的代码缺口包括恢复后旧异常计时、诊断截图失败阻止成功业务续轮，以及业务重启统计遗漏；未以删除日志错误或放宽身份检查解决。
