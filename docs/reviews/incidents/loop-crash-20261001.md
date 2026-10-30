# 100轮中断排查：第56轮退到桌面

## 结论与边界

2026-10-01 18:29–18:34按用户要求排查，没有修改生产代码、部署或重新启动游戏循环。

- 正式candidate53后台PID8916仍运行，17654接口正常，不是工具进程闪退。
- 请求`loop100-20261001-1341-756bc2cc`完成55/100；逐项只读核对run1–55均Completed、3/3段且quiescent，报告合计55份、住宿合计55次。旧批次不混入。
- run56 Failed、ROI_INVALID、quiescent=true，结果已落盘；repeat.active=false。run56战斗、报告、住宿均为0，不计完成轮。
- 失败诊断图片确实是安卓桌面；可以确认游戏不再前台，尚未取得Android崩溃栈或退出原因，不把“退桌面”包装成已证明的原生崩溃/OOM。
- 排查开始时目标ADB地址16448不在设备列表，未找到原实例2的15.0进程；可见的14048/7900属于另一台12.0实例，不应误认目标仍运行。无法由此单独确定原模拟器退出原因或时刻。

## 失败链

1. 第56轮启动的生命周期观察记录application_running/foreground、instance_running及vpn_ready均true。
2. 18:05:03.880进入Task_TimeLeap_Entry，18:05:04.161提交Task_TimeLeap_RuinsEn（点击进入荒屋）；此前公会返回和离开均有确认回执。
3. 等待结果期间出现CAPTURE_METADATA_NOT_READY和一次ADB_TIMEOUT；原栈内只读恢复重试4次，窗口11.654秒，没有重放该输入。
4. 保存的metadata_invalid_pixels仍为900×1600，但内容已是横向安卓桌面。后续日志显示实际可读帧变为1600×900。
5. 恢复读图后继续执行原游戏识别。NativeInputGate::capture在非游戏前台保留实际尺寸，这是读取生命周期现场所需；但上层未先分派非游戏前台，仍传入900×1600游戏配方。
6. recognition::Service::evaluate检查ROI在当前图像范围内，抛ROI_INVALID。18:05:16.890输入结果unconfirmed，根任务失败；完整结果文件最后写入约18:05:19。

这不是单纯“素材阈值不够”或“点击次数不够”。横屏桌面不是游戏页面，不能靠扩大ROI、裁掉越界或降低阈值继续游戏识别。

## 源码缺口与后续方向

- `next/native/devices/native_input_gate.cpp:44`：非游戏前台保留真实raw.size。
- `next/native/recognition/service.cpp:101`：ROI越界作为识别错误返回。
- `next/native/runtime/flow_executor.cpp:740`：AwaitResult先检查Overlay及业务后置；仅NoHit才进入check_unexpected。ROI错误会先结束等待，不能依靠后面的NoHit异常分派补救。
- 进一步对比外层发现第二道拦截：`next/native/runtime/native_run_coordinator.cpp:401`在调用observe_lifecycle之前，遇到result.unresolved_input直接break。本轮结果确实unresolved_input=true，所以即使存在退出恢复策略，也未读取当前游戏存活状态或生成恢复计划。不能把“禁止重发未确认副作用”扩展为“禁止只读检查游戏存活”。
- 恢复方向：在对任何游戏/覆盖层ROI做识别前，按当前帧前台包及视口分派生命周期观察；确认游戏退出则走已有启动/恢复链，确认模拟器实例退出则走绑定实例恢复。保留已提交输入及其待确认状态，不因回桌面自动重复付款、提交或跳轮。配置ROI错误仍应明确报错，不能吞成NoHit。
- 当前last_valid_frame诊断也报DIAGNOSTIC_FRAME_IDENTITY_INVALID，只有metadata_invalid_pixels成功保存；应随上述前台分派一起核对非游戏现场的诊断身份，不伪造游戏许可。

## 旧Python与新版对照

对照对象是当前仓库`src/script.py`的旧Python链，包含此前维护过的恢复改动，不将这些代码全称为原作者原版。

| 环节 | 旧Python实际行为 | 新版当前行为与缺口 |
| --- | --- | --- |
| 截图前台检查 | ScreenShot（986行）最多每3秒调用dumpsys检查前台；没有wizardry就先restartGame，再继续CaptureScreen | NativeInputGate记录前台包，但允许按非游戏实际尺寸返回帧；游戏识别前没有等价分派 |
| 非前台但进程仍在 | 没有精细区分，通常进入force-stop后重启 | LifecycleObservation已有running/foreground两字段；外层失败分类只有application_running=false，没有单独前台丢失分支 |
| 游戏应用恢复 | restartGame（1642行）先force-stop，检查退出，再am start；WaitGameBootReady（1580行）处理启动弹窗、下载、继续并等待稳定页面，默认120秒 | 已有StartApplication、EnsureVpn和Boot子流程；但本次ROI错误及pending门禁阻止入口抵达 |
| ADB/模拟器恢复 | DeviceShell/ScreenShot遇到ADB错误进ResetDevice；CheckAndRecoverDevice（593行）最多20次连接，启动未运行模拟器，成功后EnsureClashVpn；应用恢复失败先修ADB再升级模拟器重启 | recover_observation（device_session.cpp:414）支持绑定实例恢复；实例新启动才执行EnsureVpn/StartApplication，只读连接恢复不等于游戏前台恢复 |
| 任务续行 | restartGame抛RestartSignal；RestartableSequenceExecution（1737行）捕获后重做其包装操作，Farm又包装QuestFarm/DungeonFarm | 外层维持业务/输入事实，但unresolved_input在生命周期观察前阻断；要区分允许检查、允许拉起与允许重发业务输入 |
| 横屏与识图错误 | NormalizeScreenshotImage（581行）将1600×900转置为900×1600；旧_check裁剪ROI、cv2异常返回无匹配 | 新版保留真实尺寸与ROI严格校验正确，不能为兼容旧行为转置桌面或吞掉配置错误；需要先分类非游戏上下文 |

应承接旧版的应用检查入口、启动后收敛和恢复后续行，而不是复刻其全局taskkill、任意非前台都force-stop、ROI越界回整图或整段副作用无条件重放。新版修复需要在原唯一生命周期链内完成：先只读观察当前实例/应用与输入事实，选择已有恢复动作，再用独立新帧重认原业务阶段。当前对比仅审查源码，没有实机操作或修改生产代码。

## 证据与现场操作

证据根：`next/.local/c11-flow-product/data/runs/DC410F21-F06F-4E7A-8681-C3DC4E104E72/56`。关键文件为result.json、action-timing.jsonl、recognition-memory.log及diagnostics/1.png。运行记录与用户配置未清理。

本次正式只读取图因目标连接消失返回DEVICE_RECONNECT_FAILED；旧preview帧时间很早，不能作为当前游戏状态。

排查发生一次操作失误：18:30:56误调用MuMuNxDevice.exe info -v 2而非MuMuManager.exe，短暂拉起了实例2/PID2596。已核对命令行与创建时间，只结束本次误启动进程，没有结束原12.0实例或后台。随后正确MuMuManager查询确认实例2的is_process_started/is_android_started均false；未启动游戏或循环。该操作不被描述为“全程无现场变化”，也不拿误启动后的元数据推断原退出原因。

未发现对应Windows Application Error/Windows Error Reporting证据；因原Android连接已丢失，未取得logcat/lastExitInfo，不继续反复启动来补造现场。
