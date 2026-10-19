# 2026-10-15 停止循环与重启排查

## 范围与停止事实

用户要求先停止、排查重启原因，不再启动循环。本轮仅通过原Application停止接口停止当前请求、只读排查并保存证据；未修改生产源码、配置、流程、驱动或VPN，未构建、部署、启动游戏/模拟器或续跑。定时任务wvd仍为PAUSED。

- 实例：E9BDAE77-86A3-43D8-BD25-ED08F3B1EE15，PID36068，start_filetime134364625896589622。
- 请求：giant-100-panel-handoff-20261014-16df40b732224180af41c254a89e883c。
- EXE：next/.local/hf-candidate-261014/automationd.exe，实测SHA256 fdcc6b0ce1edd66c25bc448facfed30b5e2d85321d00d1f155a006dd0b617d76。
- 原端口17654及正式data-root不变。
- 逐份核对run1至17的保存回执，均Completed/details_complete=true，累计17/100。run18为UserStopped/STOP_REQUESTED，不计成功。
- 停止后busy=false、quiescent=true、repeat.active=false；最终run18回执pending_inputs=[]，inputs_released=true、unresolved_input=false。停止时快照和最终回执都保留，不把缓存步骤当作最终步骤。

## 两种不同的重启

按实际事件类型和操作过滤，不按包含字符串的整段execution.changed计数。run1至17没有bound_game.restart，也没有instance_restarted_in_window=true的恢复完成事件。run18有一次模拟器恢复和一次主动游戏重启；模拟器重启同时拉起游戏不是额外一次主动游戏重启。

### 1. 实例2宿主渲染进程崩溃

run18 generation2先点击真实郊外菜单的“第十區-要塞3F領主室”：事件484接受坐标(645,883)，当前帧11；之后帧12是黑色转场。随后事件501为ADB_TIMEOUT（screencap预算8000ms、实际8013ms），503/505为MuMu元数据查询超时，507为DEVICE_INSTANCE_RESTART_REQUIRED，531确认instance_restarted_in_window=true及恢复原连接。

Windows Application事件1000在2026-10-15 00:46:36.913记录：

- 应用MuMuNxDevice.exe，PID0x6408=25608。
- 模块C:/soft/MuMu Player 12/nx_device/15.0/device/libRenderer.dll。
- 异常代码0xc0000409，偏移0x929299。
- Report ID a45a5650-62cb-4da5-a540-47de2a02910d。

PID25608与实例2的shell.log.1匹配；新shell进程PID74236于00:46:49.618启动。设备自身诊断00:47:25.805记录context_restored，原执行回执保存明确的实例退出身份。该条证据证明渲染进程崩溃，不能据此认定具体驱动缺陷、内存泄漏或WVD取图致因。本次不能套用旧日志中的nvoglv64.dll/901故障。

### 2. 恢复后的标题加载累计60秒触发主动游戏重启

恢复后进入原ReconnectBoot。免责声明输入事件572/579接受，584确认页面离开。此后出现未命中候选的加载等待，连续异常计时重新开始；后面命中标题并点击，但标题未离开。

- ReconnectBoot_Title接受事件625、633、642、650、659、668、676，共7次；坐标(450,1450)，没有input.transport_failed或DeliveryUnknown证据。
- 事件680：CONTINUOUS_EXCEPTION_TIMEOUT，elapsed_ms=60294、limit_ms=60000，操作bound_game.restart。
- 第一次标题点击625至事件680实际25565.7363ms；60秒包括前面的约34.7秒加载/轮询，不是标题点击后单独等60秒。
- 事件681保留重启前原帧：游戏标题Logo、没有可见“Tap to Start”；此帧并非角色面板、死亡页或战斗页。
- 设备诊断00:49:03.273记录observation.application_restart_requested，reason=CONTINUOUS_EXCEPTION_TIMEOUT；00:49:15.547再次context_restored。该次instance_restarted_in_window=false，仅重启游戏。
- 原标题pending完整13字段保存在run-18-critical.json：Sent、attempts=7、delivery_unknown=false。未将本机提交成功当成页面推进。

这解释了第二次重启由谁触发和计时依据。现有图片/回执不能证明标题未推进到底是服务端连接慢、游戏内部停滞，还是点击时尚未可交互；也不能将其冒称为scrcpy发送失败。脚本当前确实将标题出现前的未命中加载时间计入同一60秒异常窗口，标题可点击与仅Logo加载状态未在该条件中区分。

## 实际源码来源

- next/native/devices/device_session.cpp，recover_observation：确认原实例is_process_started=false后签发退出证明，再沿已有RestartInstance恢复；连续异常请求才执行StopApplication。
- next/native/runtime/flow_executor_events.cpp，check_unexpected：未命中时建立exception_since_。
- next/native/runtime/flow_executor.cpp，tick：连续异常达到exception_timeout后先重查当前结果，仍未推进才请求bound_game.restart。
- next/native/games/wvd/recovery/boot.cpp，boot_workflow：Title场景匹配Logo，固定点(450,1450)，原页仍在时3000ms补点；后置为Logo离开。免责声明与标题并非完整游戏就绪证明。

上述名称均来自真实代码/日志，不是新增设计术语。此次没有调整期限、门禁或流程。后续若授权修复，应以当前标题原帧和实际启动阶段验证“加载中”与“可开始”的判断及恢复计时，不通过重复点击、放宽Unknown保护或增加外部守护掩盖。

## 证据与边界

独立证据目录：next/.local/rr-261015/。

- stopped-current.json、service.json及run-1至18-result.json：原停止事实与累计。
- restart-index.json、recovery-index.json、results-index.json：严格过滤的重启和完成索引。
- run18-execution-events.jsonl、action-timing、diagnostics、recognition-memory、memory-lifecycle：原运行证据。
- run18-recovery-details.json、title-inputs.json、device-snapshot.json、device-recovery-connections.json：设备恢复、7次点击、原未决账目。
- windows-application-errors.json：原Windows崩溃事件；mumu-shell.log/.1、VBox.log/.1、vboxmanager.log/.1：原宿主日志副本。
- run-18-restart-681.png：重启前标题原帧；115张run18最近图片独立保留。其中20261014T164612763_r18_f11.png为郊外菜单，20261014T164614401_r18_f12.png为随后黑色转场。

没有同次故障的原生崩溃调用栈，libRenderer.dll内部根因仍未归因；没有服务端网络证据，标题停滞原因仍未归因。RESOURCE_UNRESOLVED保持，不以此次模块名/异常代码代替内存分配归因。保持停止，等待用户下一步安排。
