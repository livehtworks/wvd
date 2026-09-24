# WVD 原生运行链修复设计：职责、所有权与返回契约

日期：2026-09-24。准确基线：`53718f1bf54980fbdbb7bdccf753cb0b77c2a95f`。
本文件说明本包候选代码和本地必须补齐的接线，不是“架构已通过实机”的声明。

## 1. 不改变的产品目标

Windows / C++20 / Vue 仍是唯一当前产品路线；不回装 Maa，不接入另一套通用流程框架，不建设安装器、更新器或跨平台部署。
保留配置、策略组、任务点、探索、流程编辑、公共步骤/块、事件规则、素材别名/mod、历史结果、启动停止、诊断等现有功能。旧 `src/`、旧配置、mod、日志、用户流程及旧发行目录均不是本包修改对象。

唯一执行链：

```text
Vue 已保存的步骤、块、任务、事件规则
  -> Application 校验请求和冻结当前配置
  -> WorkflowRepository 取得依赖闭包
  -> 游戏编译层：WVD 语义和公共定义 -> FlowProgram
  -> NativeRunCoordinator：Run/设备租约/终态
  -> NativeExecutionSession：一个工作线程上的会话
  -> FlowExecutor：调用帧、等待、事件、输入回执、选路
     -> RecognitionService：只读图像计算
     -> NativeOperations：WVD 业务确认与策略/计数
     -> NativeInputGate：单次真实输入许可
        -> DeviceSession：控制准备、截图、ADB、scrcpy
           -> 自有 CaptureHost：只读厂商截图调用
```

“唯一执行器”不等于把一切放进一个类。Run 协调、步骤执行、业务确认、物理 I/O 分层；只有 FlowExecutor 推进流程节点。

## 2. 层的职责与禁止依赖

| 层 | 拥有的职责 | 不应承担 |
|---|---|---|
| `workflow` | 轻量 Step/Definition/事件规则和序列化 | ORT Session、OpenCV算法、ADB、线程、WVD 地点或技能 |
| `runtime/flow_executor*` | 当前步骤、调用栈、事件作用域、后置等待与总期限 | 硬编码王城/公会、图片文件、VPN包名、战斗策略消耗规则 |
| `runtime/native_run_coordinator*` | 请求、Run、设备租约、会话所有权、结果保存 | 识别具体界面、直接发送触控 |
| `games/wvd/native_operations*` | 从已确认观察更新 WVD 状态/策略/统计 | 创建线程、再次运行一个执行器、绕过门禁输入 |
| `recognition` | 帧校验、模板/OCR/自定义识别，返回 Hit/NoHit/Error | 因识别到按钮而自动点击、重启设备 |
| `devices/native_input_gate*` | 证据消费、epoch、应用/区域/权限/停止准入 | 持有流程事件栈、用全局两秒限制后置确认 |
| `devices/device_session*` | 已绑定设备的连接、截图与输入通道 | 猜测任务进度、识别失败后自行重启 |
| `app/api/web` | 产品装配、持久数据、请求与状态展示 | 前端生成假 Running、靠测试 EXE 执行业务 |

本包把事件栈相关方法放在 `flow_executor_events.cpp`，仍是同一个 FlowExecutor。CMake 取消 workflow 对整个 recognition 库的反向拖入，游戏层明确链接其需要的识别能力。
`BusinessConfirm` 的绑定名由游戏编译层填入，通用执行器不再选择 `WvdConfirm`。

当前工厂“编译期 JSON -> typed FlowProgram”仍属于原 R02 的过渡实现。必须保留唯一降级/映射入口，不能在各任务加入另一份 mapper。继续强类型化时，当前参数、次数、错误出口、延迟、返回守卫、禁用事件和 source_path 的语义都要保留，不因删除旧函数而丢掉这些修正。该转换未完成前如实保留 R02 PARTIAL，不能把重命名字段当成完成。

## 3. 帧的生命周期

1. `NativeInputGate::capture` 在需要输入的会话中先准备控制通道，再取用于识别的帧。握手不能发生在选定坐标之后。
2. 一轮场景/目标/事件分类持有自己的 FrameEnvelope 值。像素通过已有 shared ownership 复用，不必深拷贝整张图。
3. optional 缓存 reset 只撤销缓存，不得销毁本轮识别仍在引用的对象。
4. 业务确认使用 `selected_frame ? *selected_frame : capture()`。不使用 `value_or(capture())`，因为它会提前执行参数。
5. 输入许可消费后推进 epoch 并撤销 last_frame；只有新取帧才能支持下一次输入。其依据是操作身份，不是一个新的短 TTL。
6. 后置观察成功不因识别耗时超过两秒而作废；真实设备/资源/连接代次、坐标系和操作序列关系仍需核对。

## 4. 输入与结果不是同一个事实

```text
未发送 -> 已提交到传输 -> 观察结果
                   \-> 送达情况未知
```

`SubmissionState::Unresolved` 也要先保存 PendingInput，不能先返回错误、再让 has_unresolved_input() 得出 false。物理触点释放和业务结果未知要分开：UP 清理成功不等于领取/报告结果成立；业务结果未知也不等于某个按键必然仍按着。

一个 Input 必须关联一个明确的 AwaitResult。派生观察节点继承原步骤的 max_hit、disabled_events、来源、错误边与预算。编译后校验这一点，不能等第二次输入已经发出才发现观察节点次数用完。

本包没有把每个原生任务的副作用语义改成新的猜测表。导航、领取、报告等操作能否在事件后重新规划，必须以现有业务定义的结果条件为依据；没有证据时不重发。

## 5. 事件作用域和返回

事件分类顺序：覆盖层优先；主流程的正常条件其次；主流程不匹配时检查遭遇事件；一次全部 NoHit 仍可在该步骤业务预算内继续观察。

规则继承保存 `(规则, 声明规则的实际调用帧)`。不能只保存返回节点字符串，然后在触发事件的内层定义里按同名节点查找。

处理器返回后：

1. 撤销旧选择的帧和坐标。
2. 等待已处理覆盖层真正消失；同一事件仍可见时不得递归重复进入自己。
3. 用新帧执行原作者文档的 `resume.guard`。
4. 要被展开的调用帧仍有输入回执时，重新确认该输入本来的后置结果；不能只用“又看到地图”伪造领取/报告成功。
5. 全部相关回执得到正面证据后，才展开到规则的实际所属帧、进入声明的重新规划目标；命中次数继续累计，观察阶段总期限不清零。
6. 仍未确认则等待；送达未知或无法合法展开时保留阻断状态和回执，不自动重启或重发。

本包提供该通用恢复路径，但**不凭空把导航动作声明成幂等**。若某个现有导航的原后置条件本身不能代表“返回后可继续原目标”，本地需在 WVD 导航块内表达明确的“当前目标已到达/尚未到达”的重新观察步骤。禁止用全局清除 pending 让它跑过去；该业务接线必须由实机行走遇怪轨迹验收。

原生 `task_stage` 内部 Dispatch 与作者事件处理必须确定唯一责任者；同一场战斗不能两边都接管。此项仍由 Codex 在正式装配中复核，不由本包对某个测试图通过作代替。

## 6. 计时归属

- Run 总墙钟期限固定，不因 handler、子调用或重入重置。
- 普通子调用时间与额外事件时间区分：原有父调用节点的后继选路等待不重复计算整个子调用，但业务 phase 仍按其原约定计时。
- 额外事件和退出动画期间，对所有挂起祖先按“是否处于暂停区间”累计一次，不能按嵌套深度倍增。
- Wait、post-delay 和 Await 初始等待仍会按既有节奏检查事件；100ms 是轮询间隔，不是游戏响应期限。
- 编译期 post_delay 已进入执行表示，输入的延迟只在输入后/结果观察前消费一次，其他步骤的 post_delay 也不得忽略。

## 7. 线程与对象所有权

| 对象 | 所有者 / 存活边界 |
|---|---|
| FlowExecutor / 调用帧 / 业务变更 | Session 工作线程，HTTP 不直接访问可变栈 |
| RecognitionService | Session 的 shared_ptr 成员；停止线程持有 Session 时服务也必须存活 |
| FrameEnvelope | 当前分类循环与缓存分别持有值，像素共享不可变 |
| DeviceSession | Application/Run；结束前保留到物理通道清理有结论 |
| CaptureHost 句柄 | MumuCaptureClient；明确退出前不在活动路径丢句柄 |
| scrcpy控制socket、转发及临时文件 | 本次 ScrcpyControlClient；不处置别人的转发/全局ADB |
| 发布资源 | 运行快照/lease；运行中不读取编辑中的资源 |

控制面只发取消和读取线程安全快照，不 join 工作线程、不持数据锁等待厂商调用。门禁的 dispatch 锁串行取帧/输入；stop() 不获取该锁。Session 销毁顺序保证执行器/ports 先结束，识别服务后释放。

OCR 取消是粘性的，覆盖引擎尚在初始化的窗口；已发布引擎通过它实际使用的 RunOptions 请求终止。取消返回不意味着 DLL 已经退出；所有者必须继续等实际调用返回后再释放。下一次会话使用新的服务，不在旧推理仍运行时 UnsetTerminate。

## 8. 设备控制与清理

- scrcpy 固定 3.3.4；scid 限制非负31位，与 server/socket/清理标识一致。
- ready 在 dummy byte 和设备 metadata 读完后才成立。socket 收发使用有取消检查的有限等待，不把 TCP connect 成功当作应用输入已可用。
- 关闭剪贴板同步和隐式唤醒；只启用控制通道，不加视频、音频或桌面窗口。
- 跟踪已发送的 Down 与 KeyDown；停止只补齐本工具已经持有的释放，不增加新的业务输入。
- 半个报文已写出时不拼接另一条 UP 造成协议串包；保留未确认清理状态。
- `prepare_input_channel()` 在取帧前完成。`execute()` 不再悄悄建立控制连接后使用旧坐标。
- VPN UI 同样先准备控制，再取得当前授权页，阻塞操作返回后再次检查取消，并把取消谓词传到发送边界。
- CaptureHost 加入 Job 失败时，仅用本次 CreateProcess 返回的自有进程句柄清理；正常入 Job 才使用 TerminateJobObject。
- 活动 disconnect 必须先确认 close 成功，再释放 backend/租约。析构兜底不等价于已完成清理；Application 最终退出/失败初始化路径仍需本地复核，不能拿 noexcept 掩盖清理失败。

## 9. 维护约定

注释写明“为什么/归谁/失败后怎样退出”，不堆砌逐行中文。新增公共能力先确定参数、识别条件、结果与返回语义，后接编辑器；不要按任务名给通用 runtime 加分支。

新程序资源 schema=2 纳入事件返回守卫、禁用规则、歧义预算与业务绑定。作者文档格式不因此批量改写；新 Run 重新编译，旧运行产物只读，不拿旧 schema 的已编译包继续运行。不要重新封存同一 revision 却执行不同内容。

文档更新位置：将本说明链接到 `next/docs/architecture.md` 和模块 README；将实施状态写入 `native-removal-acceptance.md`，保留旧结果的历史含义。不能用本设计文件替代真实运行记录。

## 10. 资料及使用边界

- 原始代码/业务依据：`livehtworks/wvd@53718f1` 的 FlowExecutor、NativeOperations、NativeInputGate、DeviceSession、NativeRunCoordinator、native_program、serialization 与 native-removal-acceptance.md。
- scrcpy 固定协议源码：https://github.com/Genymobile/scrcpy/blob/v3.3.4/server/src/main/java/com/genymobile/scrcpy/Options.java
- scrcpy 控制协议/连接说明：https://github.com/Genymobile/scrcpy/blob/v3.3.4/doc/develop.md
- Win32 Job 加入与失败返回：https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-assignprocesstojobobject
- ORT RunOptions 取消边界：https://onnxruntime.ai/docs/api/c/struct_ort_1_1_run_options.html

这些资料用于核对协议/生命周期，不是本项目实机通过的证据。
