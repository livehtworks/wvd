# 导航分类失效导致循环中断

## 本次停止

- candidate57请求`loop100-20261001-1942-candidate57`实际完成8/100（run2–9），run10为第9次尝试，Failed、静止且落盘，不计完成。
- 停止码`navigation.map_target.budget_exhausted`是编译器通用错误出口，不代表地图目标真的找不到。原事件seq387记录实际原因`FLOW_STAGE_TIMEOUT`，源节点`Task_FirstDungeon_Route0_Stopped`。
- 自动移动已确认，接着进入Moving、选中Stopped；该节点首次执行出现`observation_not_confirmed`，后续`step_guard_not_confirmed`。约62秒后进入通用错误出口。
- 失败截图`next/.local/c11-flow-product/data/runs/9F2486FA-2564-45F0-B22F-9D94133FC85D/10/diagnostics/1.png`明确是蝎女战斗页。生命周期记录游戏存活/前台、实例存活、连接及VPN正常，没有未确认输入；不是已证实闪退或VPN故障。

## 根因与修复

执行器用一帧挑选只读Observe分支，执行时再读取新帧。场景由导航停下转为战斗后，已选Observe不再匹配，却只在原节点等到期限结束，不回到原候选集。此前只有尚未发送的Input支持撤回选择，因此正常异步转场被当成异常停止。

- 扩展同一选择来源记录至Observe和带视觉guard的Route；没有新增分派线程或另一套导航实现。
- 只读分支在新帧失效时撤回选择，恢复原候选集与原分派时间；重新识别Encounter/退出/地图等后继，不增加无条件点击、不降阈值。
- 已提交输入、Await、带业务副作用的Operation不走这条撤回路径；已有pending不撤回、不重发，原读取恢复及停止契约保留。
- 命中计数只撤销尚未执行的选择；max_hit=0的观察节点不存在计数项，不访问不存在的map元素。
- 诊断增加`selected_branch_no_longer_matches`及原分派节点；保持原期限，反复抖动不无限延长等待。

## 验证与部署

- 有限生产执行器检查`--selection-race`共3场景：带guard观察失效、无guard观察失效、反复抖动保留原期限，全部通过，不提交输入。证据`next/.local/logs/selection-race-check-20261001.log`。
- 原读取恢复7场景复核通过，包含停止、应用前台、游戏重启、送达未知和副作用绑定；证据`selection-race-recovery-20261001.log`。不跑全量矩阵，也不冒充实机。
- candidate58已部署同一17654和正式data，PID21688、服务实例`7FC94D74-C198-437E-8A90-E2E15766B9C4`；管理脚本正常退出candidate57，未关闭游戏/模拟器/VPN。配置SHA256保持`93E9EC76B6E6E709CA9AB221A92EA23EA1B41CBCC8183BC81010D43B290C2F38`。
- 沿用用户100轮授权续跑剩余92轮，请求`loop92-20261001-selection-race-candidate58`，新运行实例`EDD91B92-8FCF-4047-8653-9DEF6E89FDCE`。run1已Completed、3/3业务段、静止且落盘，报告及住宿pending均false；冷启动现场收敛与本轮业务累计记录战斗2、报告2、住宿2，不删原阶段的已执行账目。日志位于正式`data/runs/.../1/result.json`，未改写失败run10。
- 实机事件seq614在`Task_FirstEntry_Step0Fallback2Present`捕捉`selected_branch_no_longer_matches`，恢复原候选集后继续。随后地图导航成功转入Fight，战斗结束、返城、报告、住宿及退出旅店全部完成。此证据证明自然分类失效已经过正式新路径；没有声称实机恰好重现原Stopped节点的每个时序。
- 首轮完成后工具自动进入run2，repeat完成1/92；与原8轮累计为9/100。未创建外部循环器，未保证尚未完成的91轮通过。最终EXE SHA256 `559e5c0842501a9f3489c7f56f6e85815ea9baec48b23b67bb738881ee16e45a`。
- 原8轮保持历史落盘结果，未确认的新轮不计入完成。本轮不commit/push；不修改用户配置、历史结果或其它实例。

## 超时与设备恢复后续修复

- candidate58只修选中的只读分支在新帧失效，不能覆盖节点最终超时。candidate60在`route_error()`先运行已注册异常处理，再对同一子图明确标为已知的只读场景取新帧复核；真实命中才转场，未命中仍保留原错误。导航图标记Encounter、WrongFloor、Exited。一个场景最多通过该超时出口重分类一次，避免闪烁页面无限续命；pending输入、事件恢复和副作用动作不走此路径。
- `DeviceSession`先由绑定实例元数据证实实例进程退出，发出`DEVICE_INSTANCE_RESTART_REQUIRED`让执行器在启动命令前把连续读取窗口从普通60秒扩到180秒；随后只恢复原实例、ADB、VPN和游戏。未证实实例退出仍保留60秒。外层3次恢复硬上限移除，任务总墙钟、取消、实际生命周期证据及未确认输入保护不变；不是无条件重启或重发输入。
- 定向构建和`test_native_flow --timeout-reclassification`、`--instance-start-window`、`--selection-race`、`--closure-recovery`及`test_native_coordinator --capture-context`通过；完整构建日志在`next/.local/logs/timeout-recovery-build3-20261001.log`。`test_native_author --bounty`以真实悬赏/委托帧核对页面区分通过；旧`--dispatch`测试因`PUBLIC_STEP_LIBRARY_NOT_ASSEMBLED:chest-reward-continue`未通过，不计验收。
- candidate59实机首轮停在繁中“委託清單 / Request List”，`Task_InspectBoard`未将该已知页面交给既有公共切页链；主动停止并保留UserStopped结果，完成0轮。candidate60补上委托页入口，不新造坐标或改变报告/付款规则。候选60服务PID30680、实例`451B5A7F-5C6D-4E8E-88E5-0AF4B763FD8D`，同一17654和正式data。
- candidate60请求`loop90-20261001-timeout-recovery-candidate60`，实例`1840D702-F5E1-4506-A6FA-2A2B3DF0700A`的run1、run2正式Completed、各3/3业务段且落盘；run1结果在`next/.local/c11-flow-product/data/runs/1840D702-F5E1-4506-A6FA-2A2B3DF0700A/1/result.json`，战斗1、报告2（含上轮遗留报告）、报告和住宿pending均false。run3已自动开始；截至记录时完成2/90，不能据此预报后续88轮成功。运行中模拟器退出与当前场景恰好在超时边界切换尚未自然出现，相关分支只有受控证据，不标记为实机通过。

## 2026-10-02 恢复交接与连续异常

- 后续实际结果修正上述观察快照：candidate60完成40轮，第41次尝试失败。run41在进入荒屋的普通菜单中证实实例退出，原调用栈为`Entry/Task_Leap → Task_TimeLeap_Entry/Task_TimeLeap_RuinsEn@await`。绑定实例、ADB、VPN、游戏的读取恢复耗时49919ms并成功；实际跳轮尚未点击。问题是父业务提前记录了`transfer_pending`，随后原执行器退出会话，外层未确认副作用门禁拒绝重规划。不是“重启没成功”，也不能据此确认Android崩溃的底层原因。
- 修复在同一Session内恢复菜单的原候选集，再执行已注册Boot事件；保留父业务Prepare和阶段，不重跑根任务，不把旧菜单点击记为成功。实际跳轮及普通金币住宿按用户授权允许重试，使用同子图的显式只读`retry.restart_from`；编译器验证目标并同步命名空间。金币付款移除累计3次硬停止，保持5秒间隔、真实提交次数/可能金币花费和回执。绿色/紫色宝石购买与送达未知的保护不取消。
- candidate61已部署上述修复；新请求`loop50-20261002-menu-restart-candidate61`的run1在启动门禁132.172秒后报`OBSERVATION_PHASE_TIMEOUT:wvd.boot`，完成0/50。证据在正式data/runs/`273D04D7-0C67-4B3F-A079-D859D32D0AA2`/1；诊断PNG、ADB新截图与实际模拟器窗口都为黑屏，游戏进程仍在前台。不能用“素材没识别到”解释，也不能把此轮计为完成。
- 用户进一步要求连续异常60秒重启游戏。执行器新增独立连续异常窗口：正常业务确认/已声明进行中的轮询清零；异常子图的弹窗消失、换节点和重复重试不清零。到期先保存当次帧和原因/耗时，再通过原DeviceSession停止并重新拉起绑定游戏、核对VPN、运行Boot并回到原调用栈。局部观察等待到期交给该窗口，而非立即终止整轮；取消、总墙钟、识别Error、受保护输入仍保留。只有确证绑定实例退出才重启模拟器。
- 重启前诊断复用同一帧缓冲，不额外解码，不新增线程或外部循环器。连续异常状态及重启阈值进入执行快照；恢复事件、实际生命周期和重启前PNG进入原日志。受控检查只证明执行器时序与原调用保护，部署及实机结果单独记录，不宣称所有正常下载/剧情时序均已验证。
- `test_native_flow --exception-restart`验证持续异常升级、正常ongoing不重启、取消中断；`--closure-recovery`9个场景保护原读取、菜单、金币、嵌套Prepare、送达未知和副作用回执，均通过。仅缩短注入的测试时钟，不改正式60000ms默认，不跑旧矩阵。构建/窄检查日志为`next/.local/logs/exception60-*.log`；`git diff --check`通过，仅有既有换行提示。
- candidate62真实黑屏在连续62238ms后触发`CONTINUOUS_EXCEPTION_TIMEOUT`，恢复耗时5981ms，`application_restarted=true`、`foreground_restored=true`、`reconnected=false`。未重启模拟器。Boot进入后继续原调用，战斗1次、提交报告1份、200G住宿1次并回王城；正式run1为Completed、3/3段、386.7748795秒、静止且落盘，付款和报告pending=false。原记录不改写。
- 本次实测同时发现诊断hook传入`before_application_restart`作为stage，不在RunStore合法stage集合中，seq417返回`invalid_request`。业务完成但`secondary_errors=[DIAGNOSTIC_INCOMPLETE]`，Application以`REPEAT_CYCLE_NOT_CLEAN`停止并未计自动续轮。candidate63改为既有stage `recovery_entry`、evidence_kind `before_application_restart`；保留正常诊断准入，没有吞掉该错误或修改历史证据。下一次自然触发的PNG保存尚未实机重现，不制造额外黑屏。
- candidate62的恢复事件另保存在`next/.local/menu-restart-20261002/candidate62-black-recovery.json`，正式整轮证据在data/runs/`DEC1E96C-6E7B-4E9B-9C79-5589EBC5F78A`/1/result.json。candidate63 EXE SHA256为`8F91CE6D83594BA58A85A169519FD4E596DCADE24D074306CCC6E36192D1382D`；PID36920、实例`376A721C-2962-44AB-9EF3-509269889BA0`，配置哈希不变。按已实际完成的51轮业务续跑剩余49轮，请求`loop49-20261002-exception60-candidate63`；与Application新请求自己的计数分开，不把诊断不完整轮伪造成自动续轮成功。

## 49轮运行结果复核

- 2026-10-02 10:46只读检查正式运行目录`data/runs/D22CE3AB-1E48-40A0-BF82-F5AD42F3FE4C`，run1–49都有最终result：全部Completed、3/3段、quiescent/result_saved=true，secondary_errors为空，付款和报告pending=false，diagnostics.complete=true。未用文件夹数量代替结果检查，也未启动任何测试或重跑。
- 49次战斗、49份悬赏报告、49次住宿；平均232.67秒/轮。run49耗时233.1690385秒，最后动作计时及result在北京时间07:46:51落盘。seq970为提交报告子流程检查点、1122为住宿子流程检查点、1132为根业务检查点、1136为Completed终态；住宿退出回王城已完成，符合用户关闭前停在王城的观察。
- 剩余49轮完成，加此前51轮业务为100轮；candidate62的诊断缺失历史不改写。当前后台/API及MuMu实例进程均不在，用户报告升级时游戏自动关闭。循环已在关闭前完成，并非此刻在中途等待恢复。没有独立证明进程终止的外部来源，此次不重启或恢复循环。
