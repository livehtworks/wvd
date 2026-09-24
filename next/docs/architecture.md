# 当前架构

## 唯一执行链

```text
Vue 工作台 -> 同源 HTTP API -> Application
  -> ProfileStore / WorkflowRepository / 资源快照
  -> 作者定义或 WVD 原生任务生成器 -> 发布期编译为 FlowProgram
  -> NativeRunCoordinator (一个 Run、一个设备租约、一个工作线程)
  -> NativeExecutionSession -> FlowExecutor (唯一节点/调用/事件栈)
       -> RecognitionService -> OpenCV / ONNX Runtime CPU
       -> WVD 业务状态和原子操作
       -> NativeInputGate -> DeviceSession
            -> MuMu 只读 CaptureHost，失败时指定设备 ADB 截图
            -> 指定设备 ADB 查询/应用及 VPN 生命周期
            -> scrcpy 锁版控制通道发送输入
```

`contracts/` 只保存中立值和结果；`workflow/` 保存强类型执行表示；`runtime/` 持有推进、事件、回执、停止和终态；`games/wvd/` 拥有任务规则和业务事实；`devices/` 独占设备 I/O；`app/` 是产品装配者。HTTP 线程不执行识图或任务节点，Vue 不持有设备或业务状态。CaptureHost 只隔离不可协作取消的厂商截图调用，不参与流程与输入。

一个输入只提交一次，页面结果由独立 AwaitResult 观察；识别 NoHit、Error 和外部阻断分开处理。收到停止请求先关闭新业务输入准入，随后取消等待并回收自有通道；未确认清理不能报告静止。初次启动可检查/开启已配置 VPN 并确保游戏在前台；普通续段不重复初始化。仅明确确认的 Pause 物理冻结或有已记录 `wait_7300` 意图的未知跳轮允许应用重启；后者先完成可取消等待。不重启模拟器，也不凭视觉 NoHit 重启。

作者文档仍由唯一 WorkflowRepository 保存。运行前封存作者闭包、资源内容与版本，编译为 FlowProgram；历史 Maa 结果只读。正式 CMake 和打包只链接原生目标。旧 SDK 集成源码及过时验证工具位于 `next/archive/`，不是当前程序或测试的执行路径。

## 尚需核对的边界

- 原生任务生成器当前使用项目中立的发布期 JSON 图描述，经 `native_program.cpp` 严格转换为强类型 FlowProgram；运行时不解释该 JSON。工厂直接构造强类型步骤尚待完成。
- 离线正式入口已证明作者流程保存重开、编译执行及一个原生任务进入其步骤；不能证明真实 MuMu 输入、Clash、繁中页面或完整蝎女任务可用。
- 王城身份必须由塔楼背景确认，城市图标只作动作锚点。悬赏跳轮刷新后不领取，讨伐后逐项提交；普通委托才有接案。哈肯“歸還”先返回郊外列表，再回城。
- 真实维护页面素材、常态白色道具店、新设备后端长期取消和资源稳定性仍缺对应证据。旧 Maa 的资源未归因结论只属于历史路径。
