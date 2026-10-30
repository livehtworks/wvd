# 恢复途中设备状态变化

## 现场证据

本次承接用户继续排查、修复并推进的指令。正式100轮仍累计27成功、剩余73；02C237E9/run3失败原账保留。原导航图及运行日志见next/.local/wvd-check-20261013-2128/。

MuMu实例2的宿主日志已独立保存到next/.local/lr-261013/。shell.log在21:27:18明确记录Graphics Driver crash、errorCode=901、crashModule=nvoglv64.dll、isGraphicsCrash=1；shell.log.1的21:01:50及shell.log.2的20:54:08也为同一模块。可以确认MuMu报告的显卡模块崩溃，不能据此断言具体驱动缺陷、显存泄漏或脚本取图是根因。Windows对应时间范围的Application 1000/1001查询未取得额外记录；本轮没有调整驱动、渲染模式、VPN或内存采集方案。

返哈肯点击前后和后续静止帧已保存。点击附近有“连接中”，后续消失而楼层菜单未出现。旧src/script.py:2907的StateMoving_CheckStop在地图差异变小时返回重新识别；3331附近返哈肯仍走原自动移动。没有依据将实体画面直接算返城完成，也没有依据新增固定坐标点击实体或认定所有操作都失效。本轮不修改这一未确定的游戏交互问题。

## 确认的代码缺口与修复

原链：Session因navigation.input_no_progress结束，无未决输入；Coordinator按当时在线状态安排EnsureVpn/StopApplication/StartApplication。StartApplication中ADB断线、实例退出，Lifecycle执行返回RetryRequired；Coordinator把所有非ReadyForBoot结果直接写成NATIVE_RECOVERY_UNCONFIRMED并结束批次。

现在保持原唯一Coordinator和recovery_policy：恢复失败后只读重查原目标，当确实变成实例退出或连接断开时，按实际原因重新选择已有RestartInstance或Reconnect计划。不重放业务输入，不改旧Session结果，不因无变化反复调用。执行器在已提交操作后观察到这两类状态变化时立即交回，不再等待已不可能满足的旧后置条件。

输入保护和人工取消先于重新选择；原设备/实例/游戏/VPN身份及观察时间均校验。新的计划仍执行原操作白名单及业务策略；没有增设旁路、外部守护、次数上限或自动点击。恢复总窗口不因重新选择而刷新，沿最初步骤时限及原整轮期限约束。

## 验收边界

扩展既有test_native_coordinator --instance-exit：操作后退出、ADB传输异常后退出、断线、设备无变化、取消、错误实例、业务策略拒绝。真实Coordinator/Session/FlowExecutor和生命周期执行器运行，硬件后端受控隔离，不把测试当实机恢复成功。既有未决输入/旧退出证明保护用例继续运行。

test_native_devices验证操作后实例退出立即返回DEVICE_STATE_CHANGED而非等待POSTCONDITION_TIMEOUT；原期限/取消/启动未前台/ADB异常用例保留。devices/runtime/app三个模块38个合同入口通过，complete=true，证据next/.local/lr-c1-261013/receipt.json。这里不是38轮游戏，也不证明显卡崩溃原因已修复。

## 构建与部署

原tools/build.py完成正式构建及package_functional打包，原manage_service.ps1通过Validate后Deploy。冻结候选next/.local/lr-candidate-261013，产品输入3893de6725fa96ffb5b1ca08c80f3018902ae3fc5b40a6f64b1275dc4b4edb20/1301，EXE523ca41423ab06390a4e76a521de30e05dc37b08a376385326330e702421916d。仅关闭核实空闲且无未决的旧02C237E9服务。

新服务5D4EEF14-913D-47E7-816E-E4E7A2F9CAB6/PID47408/start_filetime134363748774208840，端口17654和原data-root不变。40份profile/workflow JSON逐文件哈希未变；未commit/push。新请求giant-73-lifecycle-replan-20261013-aa12df65f9d6463dba715beeedc378f5仍承接原100目标，累计基数27，剩余73；旧失败回执保留不计数。实际现场验收单独记录，不以accepted代替Running。

## 实际续跑

本次已实际Running/run1：实例2由原Application启动，VPN CONNECTED/tun0/VALIDATED；Boot_Attention通过后，标题连接中持续超过60秒，事件325触发原有游戏恢复，336记录application_restarted_menu_reclassified。之后越过原哈肯实体停点，事件643进入HarkenExit_Floor，后续返城、公会整理、入副本、新战斗技能均有输入和新帧证据。启动及后续记录见next/.local/lr-261013/resume-confirmation.json、resume-events.jsonl、live-start.png、live-followup.png、live-progress.png。

这证明新候选已部署、原剩余73轮接回且现场有进展，不是新重选分支的实机故障注入证明，也不是100轮完成。新分支由前述定向合同验证，当前没有为验证故意制造模拟器崩溃。标题页自然恢复一次的事实保留，不能声称本次全程无重启。当前累计27/100，已更新原launch-ledger；每30分钟监控仅检查留证报告，不因本次人工修复授权扩大为无人值守自动修改。
