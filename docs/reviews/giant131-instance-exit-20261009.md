# 100轮巨人停止核查

2026-10-09用户反馈程序不见了。本轮只读核查，重新打开17654网页入口；未重启游戏或循环，未修改产品代码。

## 现场与批次

- candidate131工具PID28972仍存活，服务CDD3C698-8EFF-4A9E-B4FE-1980BC0541EF和17654监听正常。当前私有提交37.26MiB、工作集52.49MiB。
- 请求3c826170-3198-43b5-b20f-9ddf886ec858，协调器4131E134-709D-47D1-B9B6-F04D34484FA2；逐轮result.json为32轮Completed、第33轮Failed，未完成100轮。
- 第33轮166.7394492秒，约03:40停止；reason=MUMU_INSTANCE_MISMATCH，quiescent/details_complete=true，secondary_errors为空。
- 当前MuMuManager info -v 2返回index=2、error_code=900、is_process_started=false、is_android_started=false；模拟器实例已退出。工具接口connected=true只是会话连接标记，不能替代当前实例观察。

## 最后调用链

原始证据位于next/.local/c11-flow-product/data/runs/4131E134-709D-47D1-B9B6-F04D34484FA2/33。

1. seq1289发送Skill4Try0Confirm，进入确认后等待；action_epoch=26、basis_frame=117、attempts=1、delivery_unknown=false。
2. seq1295取前台元数据dumpsys window失败：ADB_TRANSPORT_FAILED，stderr为error: closed。读取恢复开始，未重放输入。
3. seq1297约1888ms后读取恢复失败；seq1299输入结果unconfirmed/MUMU_INSTANCE_MISMATCH。
4. seq1305收尾实际观察到instance_exited=true、connected=false、application_running=false，同时unresolved_input=true。
5. next/native/runtime/native_run_coordinator.cpp在恢复计划生成前检查result.unresolved_input并break，因而没有recovery.plan或RestartInstance动作。

## 判断与后续问题

工具后台没有消失，模拟器实例退出使第33轮终止。重启未执行的直接原因已定位到未确认技能输入阻断恢复；初始模拟器退出的来源尚未确定。03:35至03:45 Windows Application日志未取到1000/1001故障事件，不能因此判定为正常退出或排除崩溃。

后续修复需要让同一实例的退出恢复与旧技能输入账目分别处理：真实确认实例退出后可恢复生命周期，但旧技能不能冒充成功或直接重放，必须在重新进入游戏后按真实场景重新规划。MUMU_INSTANCE_MISMATCH还将退出状态与实例身份不符混在一起，需保留创建身份校验并准确分类退出原因。
