# 原生运行层

`NativeRunCoordinator` 持有请求、运行线程、设备租约、停止顺序与终态落盘。
`NativeExecutionSession` 为每个有限业务单元装配一次 `FlowExecutor`；后者唯一持有调用栈、事件作用域、输入回执和计时。设备输入仅通过 `InputGate` 与同一 `DeviceSession` 发出。

本层只认识中立的 `FlowProgram`、观察结果和操作端口，不判断 WVD 的技能、地图或宝箱。页面结果与业务确认分别保存；已发输入在结果不明时不能重发。恢复计划可声明停止可中断的延迟，只有设备生命周期端口能够执行关闭或启动，且不得因普通 NoHit 自动重启。

停止先关闭新输入准入，随后取消等待与可取消 I/O；清理未完成时保留 `quiescent=false` 和租约，不伪报停止完成。
