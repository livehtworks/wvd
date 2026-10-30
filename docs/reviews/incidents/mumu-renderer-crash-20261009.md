# 第六轮模拟器渲染崩溃

## 本次结论

candidate138协调器C37C2A8F-4A66-4D1F-B744-03AB8E8252EA前5轮均Completed，逐轮execution-events中diagnostic.application_restart=0、首次observation.recovery=0。第6轮在入本后观察Task_FirstEntry_Step1Found结果时失联，恢复链重新拉起实例。本批已证实的这次重启不是正常页被计满60秒后主动停止游戏。

Windows Application事件1000于2026-10-09 22:19:22.9707331+08:00记录：MuMuNxDevice.exe、进程0xB2A8=45736、模块libRenderer.dll、异常0xc0000409、偏移0x929299、Report ID=9b1b74bb-e6d7-4314-9310-ee2c74d00b92。模块路径为C:/soft/MuMu Player 12/nx_device/15.0/device/libRenderer.dll。新MuMu窗口PID29324创建于22:19:35，故障记录早于新实例窗口启动。

这定位了直接故障模块，没有证明崩溃由截图SDK调用、显卡驱动、资源压力或厂商内部缺陷中的哪一种引发。不能把0xc0000409直接解释为某个已证明的内存泄漏，也不能据此证明工具调用完全无关。此次shell.log没有上一回nvoglv64.dll/901签名。

## 恢复时间线

- 原入本菜单点击[645,883]已送达，进入结果观察；没有把未确认点击记成入本成功。
- 第6轮seq546：ADB截图exec-out screencap -p超时8020ms，原预算8000ms；没有CONTINUOUS_EXCEPTION_TIMEOUT或diagnostic.application_restart。
- seq548/550：两次MuMuManager实例查询超时3037/3030ms；首个查询进程创建时间22:19:22.276。
- seq552：DEVICE_INSTANCE_RESTART_REQUIRED。源码DeviceSession::recover_observation只有同身份元数据成功返回is_process_started=false时才进入这一路，查询抛超时本身不会设置该状态；原始成功元数据没有单独留存，这一约束来自生产源码与路径事件的交叉核对。
- 后续实例启动/ADB/游戏前台恢复，共68007ms、16个读取恢复记录；context_recovery确认reconnected/foreground_restored/application_restarted，instance_restarted_in_window=true。
- 最新现场已重新进入迷宫战斗，之后到住宿确认；没有重复开启一套循环，没有修改目标轮数或正式profile。

原日志：next/.local/c11-flow-product/data/runs/C37C2A8F-4A66-4D1F-B744-03AB8E8252EA/6/。MuMu shell.log.1对应旧PID45736，新shell.log对应PID29324；VBox.log.1的最后一条为Guest seems to be unresponsive，最后心跳距当时8秒。

## 内存与截图调用核对

第6轮22:19:10的system_pressure：工具PrivateUsage约551.9MiB，系统提交56.02GiB/上限60.03GiB，可用物理8.52GiB。上一轮收尾已回到工具约143MiB。全系统压力确实偏高，但没有该崩溃的分配失败栈或渲染器堆证据，不能归因到WVD/OpenCV。

新旧截图ABI签名一致：nemu_connect、nemu_get_display_id、nemu_capture_display；新版display_id已缓存，非每帧重查显示ID；正常退出有disconnect及FreeLibrary。仍存在一个实现差异：旧Python连接时获取分辨率，新capture_host每帧先查询分辨率再取像素。该差异目前只是进一步检查方向，未据此修改SDK调用、模拟器图形设置或驱动。

本轮只读调查并更新事实，没有运行压力测试、制造崩溃或扩大重启许可。渲染崩溃来源仍未闭合；循环按原授权继续，第6轮恢复中的尝试不提前计作成功。

调查结束时第6轮已Completed并实际提交结果：三段业务结算、details_complete/result_saved=true、secondary_errors=[]、本轮cycle=1、报告剩余0、无报告/住宿待确认。工具自动开始第7轮Running；当前续批6/41，原59轮加本批6轮为65/100。这证明本次恢复能够接续并收尾，不证明模拟器崩溃问题已解决。
