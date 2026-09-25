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

`Application` 还拥有一份所有相关 Service 共享的匹配工作集准入。`recognition` 层按已知 ROI/模板/方法估算叶级临时矩阵，准入采用 RAII；每个 Service 拥有跨 worker 的单次装载像素缓存，时序游戏状态仍留在独立的 `Cache::assets`。`games/wvd/vision` 只负责具体匹配与候选优先级；`platform/windows/memory_diagnostics` 负责固定槽、预开日志句柄和 Windows 内存标量。工作集 256 MiB 与像素留存 128 MiB 都是工程调度目标，不是进程安全上限或历史故障证明。

一个输入只提交一次，页面结果由独立 AwaitResult 观察；识别 NoHit、Error 和外部阻断分开处理。收到停止请求先关闭新业务输入准入，随后取消等待并回收自有通道；未确认清理不能报告静止。初次启动可检查/开启已配置 VPN 并确保游戏在前台；普通续段不重复初始化。仅明确确认的 Pause 物理冻结或有已记录 `wait_7300` 意图的未知跳轮允许应用重启；后者先完成可取消等待。不重启模拟器，也不凭视觉 NoHit 重启。

作者文档仍由唯一 WorkflowRepository 保存。运行前封存作者闭包、资源内容与版本，编译为 FlowProgram；历史 Maa 结果只读。正式 CMake 和打包只链接原生目标。旧 SDK 集成源码及过时验证工具位于 `next/archive/`，不是当前程序或测试的执行路径。

## 本轮修复后的责任边界

- `authoring/workflow_validator` 只校验作者文档结构；`storage/WorkflowRepository` 保存正文及独立的内置来源元数据，显式 CAS 同步前保留旧文档备份。固定 `expected_revision` 的调用不被自动改写。`games/wvd/tasks/run_builder` 决定任务工厂、轮数、7300 秒等待及恢复条件；Application 冻结配置与定义、编译预检后才连接设备。
- `workflow/FlowProgram` 的 Call 将子定义的业务失败作为有类型的返回，不把有效子任务 ID 或框架完成态当作业务成功。`runtime/FlowExecutor` 由唯一会话线程推进调用栈及事件栈，普通失败走调用者的失败边，致命错误终止会话；`BusinessConfirm` 只在观察到正面业务证据后改变 WVD 计数。`RegisteredOperation` 通过门禁发一次输入，结果不明时保存未确认回执并禁止重发。停止先封输入，再取消等待/回收自有设备通道，未静止不能标为完成。
- `FlowExecutor::progress_snapshot()` 只给出当前步骤/调用栈/事件与未决操作标量，Coordinator 存入历史并同源展示在工作台。HTTP 不推进流程；事件与输入的所有者仍是会话线程。
- 成功 `Return` 只校验即将弹出的子帧 pending；父输入的回执继续挂起，到事件返回后的新帧确认。根终点和业务失败仍核对全栈未确认输入。有序启动/对话候选首命中后不安排无关尾项，`all/any` 与必须全量的反证仍传播已执行的全部 Error；资源 OOM 优先于同批次 Hit。
- `resources/authoring/semantic-assets.json` 是人工配方源，打包同步到资源包并校验清单；作者及旧原生映射在编译/发布前解析。诊断的非 OCR 条件走同一 WVD 识别绑定。城市公共配方、繁中公会页及开箱/选人/奖励动态探针由同源目录生成于构建期，发布身份带源哈希且打包拒绝 EXE/资源包错配；其他内部动态 boot/地点探针尚未全部做到发布期依赖收集和语义冻结，不能宣称 R05 全面闭合。
- 公会悬赏 Reveal 由原生任务调用同一 `PublicFlowLibrary` 闭包。已处于目标页时零输入；打开列表不是跳轮或提交。英文 Report 保留旧源码的准备、提交后置确认及计数契约；繁中缺 `guild.report.action` 配方的完整任务在准备期拒绝，不进入跳轮或讨伐后才失败，也不拿奖励按钮猜提交点。

## 尚需核对的边界

- 原生任务生成器当前使用项目中立的发布期 JSON 图描述，经 `native_program.cpp` 严格转换为强类型 FlowProgram；运行时不解释该 JSON。工厂直接构造强类型步骤尚待完成。
- 离线正式入口已证明作者流程保存重开、编译执行及一个原生任务进入其步骤；不能证明真实 MuMu 输入、Clash、繁中页面或完整蝎女任务可用。
- 王城身份必须由塔楼背景确认，城市图标只作动作锚点。悬赏跳轮刷新后不领取，讨伐后逐项提交；普通委托才有接案。哈肯“歸還”先返回郊外列表，再回城。
- 真实维护页面素材、常态白色道具店、新设备后端长期取消和资源稳定性仍缺对应证据。旧 Maa 的资源未归因结论只属于历史路径。
