# 启动页输入连接失效

## 范围与现场

用户授权排查、修复、部署并续跑原100成功轮次目标，不新开100轮，不自动commit/push。正式profile和保存流程不修改，失败回执保留。已核原run1–18及续跑run20–27的保存Completed，共26成功；run19、run28失败均排除，待续74轮。

原实例5D313D2F-33ED-4BCB-B390-4F0F7EB8BE2F/run28，最终INPUT_RESULT_UNCONFIRMED:AWAIT_RESULT_TIMEOUT。截图是繁中启动“請注意”，不是死亡页；frame475/generation2是失败前最后有效帧，不是点击依据frame287。原图现为next/.local/retained-evidence/inspection-issues/startup-input-last-valid.png；13字段pending、必要事件/输入耗时及正式原结果/日志路径和SHA统一见next/.local/retained-evidence/inspection-issues/index.json。本轮另保存next/.local/sc-261013/device-diagnostics-before.json及40份用户配置/流程的哈希索引。

## 已确认的故障链

1. 选择技能等级时元数据异常，随后ADB查询报error: closed。
2. 原恢复路径在16.620秒后记录application_restarted_menu_reclassified。设备诊断明确before=1、after=1、application_restarted=true、reconnected=false：恢复了游戏，没有重建控制连接。
3. ReconnectBoot_Attention对(450,1450)提交一次点击，输入后端报SCRCPY_SEND_UNRESOLVED。控制端收尾保留held=1、unconfirmed=1、unresolved=1，证明本地已有触摸按下责任，不能把这次操作降级成“完全未发送”。原版本没有发送字节数/Windows错误码日志，不能补造具体字节数或错误码。
4. 121.2187946秒没有确认操作结果，原FlowExecutor保护停止。Session另记INPUT_RELEASE_UNCONFIRMED，因此worker已结束而busy=true/quiescent=false仍保留；不是仍在执行点击。

原scrcpy的connected()只检查本地ready_和socket句柄；DeviceSession/NativeInputGate据此跳过控制通道准备。远端断开后本地句柄仍存在，不能证明连接可用。这是代码缺陷。初次ADB断线的外部原因及历史内存问题仍未归因。

## 修复及依据

- ScrcpyControlClient::connected改为检查自己启动的客户端进程及非阻塞recv(MSG_PEEK)。它不发送游戏输入、不消耗服务端消息、不截图、不识别、不等待，也不查询ADB；空闲可用连接返回WSAEWOULDBLOCK，关闭/错误连接不再被本地句柄放行。
- DeviceSession::prepare_input_channel在失效连接上先调用原close，只有已释放自己的触摸、客户端和转发后才重新握手。仍由原NativeInputGate/FlowExecutor在准备后重新取帧；execute内不透明重连、不提交旧坐标。
- 同一DeviceSession的VPN生命周期输入准备也执行原close后再握手，保留原cancelled及新帧检查，防止漏掉该既有调用者。未新建VPN处理器或另一个执行入口。
- 新增input.channel_prepare、input.channel_ready、input.channel_prepare_failed、input.transport_failed诊断；失败记录发送字节数、完整报文数、Windows错误码及持有/未决状态。它们是本项目日志字段，不是新造的行业算法。
- 部分发送、半报文、持有按键/触摸、Unknown及清理失败继续保留；不改ROI、匹配阈值、等待上限、重試次数或游戏分支。
- 首次真实TCP验收另暴露收尾缺陷：远端已关闭而本机发送缓存仍可能接受UP，原close会误报已释放。close现先记真实连接状态，断线且仍有持有输入时不依赖本机send成功清账；保留未释放状态，只能按既有明确实例退出职责退役。首次失败回执next/.local/sc-c1-261013/原样保留，不删断言或调整预期。

来源：[Microsoft recv文档](https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-recv)说明MSG_PEEK、非阻塞WSAEWOULDBLOCK及关闭时返回0；[scrcpy官方连接说明](https://github.com/Genymobile/scrcpy/blob/v3.3.4/doc/develop.md)描述ADB隧道和独立控制socket。这里只引用连接行为，不用官方资料替代本仓库的调用链证据。旧src/script.py:1366–1372也识别启动免责声明并点击(450,1450)，旧ADB ResetDevice重试不能当作新scrcpy部分发送可盲目重放的依据。

## 验收状态

完整build.py构建及原build.run/begin_build/finish_build的定向重编、package_functional.py冻结打包已通过。新真实Windows TCP夹具使用生产ScrcpyControlClient；仅ADB启动器为隔离测试端，不连接模拟器。覆盖空闲正常、EOF但启动器尚活、连接重置、重建后完整点击、持有触摸断线不伪报释放。最终next/.local/sc-c3-261013/receipt.json的devices/runtime/app 38入口complete=true，产品输入9f3b1556f51efc9e8235c8c8621ac1ae124dda0feb6ed29ba98e43f2a34d7768/1301项。四组真实连接夹具全部通过；健康连接1000次检查耗时1–2ms是离线测量，不是实机全流程耗时。

部署前核对实例2的创建标识/端口/Android版本，以管理器只关闭实例2，实际is_process_started/is_android_started=false后，原manage_service.ps1正常关闭旧服务；未任意终止其它进程，旧业务结果仍失败，原pending不改成成功。证据next/.local/sc-261013/instance-exit-proof.json、deployed-service.json。新版在原17654及data-root部署，实例0DE7D06D-EF0A-407B-8BB6-A2BEF5F5B66E/PID63996/start_filetime134363640750279053，EXEb7534b59ad356eabd89ac2e4935f8abdf939b0890e9b4b2120a40e9eea2eaa82；40份profile/workflow逐份哈希未变。

从原Application入口提交giant-74-scrcpy-reconnect-20261013-64a3f8786c9c4c5c9f8c161ee9f0489c/GiantBounty/zh-Hant/repeat_count74，已实际Running/run1。原入口重新拉起可见实例2，VPN CONNECTED/tun0/VALIDATED。“請注意”Boot_Attention事件48接受、54确认，basis frame9→observed frame11，结果等待3240.3348ms；frame11是黑色加载转场，不当作已入游戏证明。后续标题、下载提示、公告输入均确认，继续进入战斗技能并推进返哈肯导航，事件1635/真实输入1613。证据next/.local/sc-261013/resume-confirmation.json及attention-before/after.png。原26加新repeat.completed_cycles累计到100，不能重复加completed_segments；当前26/100，新段尚未保存首轮Completed，不声称100轮已通过。

回滚入口为旧next/.local/lw-candidate-261012，通过原manage_service.ps1按真实服务身份停止新版并空闲启动旧版；保留原数据备份/哈希，不自动覆盖profile/workflows，不启动旧批次。未commit/push。每30分钟检查将跟随新请求，仍仅只读检查与报告，未来保护停止不自动抵消。

内存仍RESOURCE_UNRESOLVED；不新增WPR/ETL/系统堆采集，不把这次连接修复冒称内存修复。红色过渡页两个skull导致过早party_defeat的另一个已知问题仍未在本轮修改。
