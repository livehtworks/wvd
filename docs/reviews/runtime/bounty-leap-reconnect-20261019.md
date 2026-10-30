# 悬赏跳轮恢复后误停

## 现场与计数

2026-10-19用户要求查明当前停止原因、修复并继续同一100轮。正式实例EED629D7-2CD2-4299-9BDE-DFD9C93AA9DC的run1–50均有保存的Completed回执；加此前43E2B469的15轮及87BA907F的1轮，共66/100。run51失败、业务单元0，不计成功；后续只续34轮。

原服务17654和正式data-root不变。现场原帧、完整事件、动作计时、终态、服务身份、27份权威文件的只读副本及哈希保存在`next/.local/bt-261019/`；没有为诊断写入正式profile/workflows。

## 已证实的原因

- run51事件127已登记`bounty_leap_prepared`，含义是开始跳轮阶段，不是游戏已接受跳轮。
- 荒屋打开输入事件135接受、142确认；frame62原图仍是荒屋菜单，尚未进入轮盘目标选择。
- 事件149尝试打开轮盘，151因INPUT_CONTEXT_CHANGED拒绝，152明确rejected_before_effect，旧输入已结清；没有发出QuickLeap/Leap跳跃输入。
- 事件155为CAPTURE_METADATA_NOT_READY，157为ADB_TRANSPORT_FAILED且stderr为`error: closed`，159/161等待连接；163记录应用在恢复窗口内重启、菜单重新分类，耗时17.078秒。这里证明读连接异常及应用恢复，不证明游戏、模拟器或显卡崩溃的底层原因。
- ReconnectBoot完成后，事件375回到Task_TimeLeap_Entry，随后恢复规则重新分派根入口；388进入PendingTransfer、390进入TransferUncertain，最终quest.bounty_transfer_unconfirmed。
- 终态业务账目仍是active=true、phase=Leap、unit_matches=true、transfer_pending=true；未决输入为空。这个pending来自“准备跳轮”，不是送达未知，也不是已提交跨城传送。

调用链：`with_boot_recovery`的设备恢复规则恢复启动后重选根入口；`bounty_cycle.cpp:Entry`原来把所有transfer_pending无差别接到失败出口。跳轮导航自身已有用户授权的重做路径，但根入口在调用它之前就提前停止。这是任务阶段续接遗漏，不是素材、ROI、点击延时或角色死亡误判。

## 修复边界

`bounty_cycle.cpp`入口增加ResumeLeapTransfer：必须同时是原有效悬赏、Leap阶段、业务单元一致且transfer_pending，才直接调用同一个Leap子流程。保留原准备账目，不再次PrepareLeap、不伪造Leaped、不提前推进Reveal或增加成功轮数。

这里的unit_matches来自`quests/bounty_cycle.cpp:BountyCycle::summary/require_phase`，核对本轮当前阶段与内部段序号，防止拿另一段的准备状态继续；它不是成功轮数。transfer_pending由prepare_transfer设置，因此不能等同于输入送达未知。

既有跳轮导航仍按当前页面操作并确认实际跳轮后的场景；已知送达的跳轮重做沿原retry.restart_from授权，送达未知仍由FlowExecutor保留原输入并停止，不借任务分支放行。跨城Travel阶段、业务单元错位、付款、交账、救人和低意志力保护不改变。不新增执行器、恢复处理器、模板或外部守护，不改ROI、阈值、超时和点击时长。

## 验证与交付

- 同一既有giant_linkage夹具：旧版8个原场景通过，新增reconnect-before-leap准确复现quest.bounty_transfer_unconfirmed/Task_TransferUncertain，保留before-fix失败回执。不是修改预期来适配旧错误。
- 新版10个完整工厂场景全部完成三个内部段，包括跳轮提交前及提交后恢复；后者复用原已知送达跳轮的允许重做语义。两种恢复均只准备一次，不清准备账目，末尾pending=0、cycle=1。新增业务单元错位及跨城Travel未决反例仍失败停止且零输入，准备账目保留。实际使用正式profile/保存公共流程的只读副本，语义页面及设备传输隔离；这不是12轮游戏实跑。输出after-fix/及before-fix/。
- recognition模块c1的19入口complete=true，源输入690257888ba8f0d32de43e148131495066952158662d0464856f3b3c673ddbb2/1315，包含实际资源/旧原帧及受影响模块回归。
- 未修改的FlowExecutor另复用现有二进制定向核验input-effect-contract、unknown-input-manual-stop、unknown-ancestor-reconnect-input，三组通过；二进制hash及限定范围见unchanged-runtime-safety/receipt.json。这是未变化输入保护的回归，不冒充新产品完整构建回执。

- 原build.py辅助入口、package_functional、manage_service完成构建/冻结/部署，新候选next/.local/bt-candidate-261019，EXEd0164f8c62b34fb4bfedfdef046ddfa529033b20256c5e0fa19afb3d11a02aae，输入690257888ba8f0d32de43e148131495066952158662d0464856f3b3c673ddbb2/1315，与模块c1一致。旧gb候选保留作为人工回滚材料。
- 原管理器确认旧EED629D7/PID45836/start_filetime134368244079173126身份后正常关闭；正式新实例A93DE32D-74E4-47C7-BB19-9C7E7F1E9CD1/PID38248/start_filetime134368585657131278，仍为17654与原data-root，27份profile/workflows逐项哈希不变。没有手工补点、改VPN、改驱动、重置游戏或启动外部守护，没有commit/push或恢复定时任务。
- 新请求giant-34-leap-reconnect-20261019-0391af7da6204d69bd005d92729d198f，GiantBounty/zh-Hant/repeat_count34，基数66。resume-ledger.json保留三段完成回执来源，run51失败排除；只按66+当前completed_cycles累计。
- 2026-10-19 12:49实核新run1已保存Completed、result_saved=true、quiescent=true、completed_business_units=3、unresolved_inputs=null。战斗、返城、交账、住宿完整结束，末尾事件3232；整轮observation.recovery事件0，没有重启。run2已实际Running推进Task_RecheckOldReports，累计67/100、余33继续，不另开100。证据completed-run1-result.json及full-round-confirmation.json；正常整轮成功不冒充实机自然复现断线或100轮已完成。

这次修复的是恢复后的任务误停；导致ADB报告closed的底层来源尚未归因。新真实现场不制造断线，提交前/后恢复的正确性证据来自上述生产工厂/执行器受控回归，不冒充实机再次复现。先前哈肯无HUD停滞亦不能由本修复或一次成功解释。

人工回滚：停止匹配的新请求并确认静止，以原管理器关闭新实例；部署保留的gb候选到同一17654/data-root，核对27份权威文件哈希后由用户决定是否继续。源码前态在bt-261019/bounty_cycle.before.cpp及giant_linkage_cases.before.hpp；不通过双进程、双配置或自动回退实现。
