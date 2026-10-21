# 单人救人后的恢复停点

本报告保留2026-10-20的排查与验证过程；下文运行、监控和未提交状态均为当时记录。当前操作边界以[当前状态](../project-status.md)为准，最新循环已取消、监控暂停。

## 17:53后新故障排查

最后恢复核验为18:47的E65BF7A4/run1 Running，事件3233、原帧319、累计11/200，新段恢复0；帧319显示“可以感受到哈肯的加護……”画面，原只读过渡等待继续，不把它当整轮成功。10分钟监控已切新身份、根请求及rescue-marker-resume-261020/inspect.ps1；更新计划后不额外立即巡检。

B28814C5/run1在事件2526/2530完成救人中心输入epoch75，事件2537的epoch76后置一直未确认。2585的原帧408仍为救人手掌、圆圈和右下X；旧someonedead素材也是圆形X，但两次故障原帧的相关分数分别0.6601/0.6695，低于原0.8。原Service/门禁回放新帧失败RECORDED_INPUT_SCENE_MISMATCH:Dismiss，不再仅把它归为不可解释的游戏冻结。随后Boot超时原图为黑色，恢复深度保持5，说明旧栈复用修复有效但未处理原始漏识别。

已通过原停止接口维护停止，保存UserStopped/result_saved/quiescent回执，0业务单元不计成功。原epoch76及Title的epoch128/attempts2均Sent/nonUnknown，完整13字段未清未伪确认；停止后两次独立新取图均为同一Pause/悬赏卡画面，不能称当前仍黑屏或据此断言官方完全无交互。现场、日志、输入耗时和复核的11成功/27权威哈希在next/.local/rescue-boot-check-261020-1753/。

已完成同一party_death标记配方的真实外观补充：原PNG不修改，只从原资源中的[762,1443,91,89]匹配圆形X，原someonedead外观保留；搜索范围、0.8阈值、完整场景排除门禁和(450,800)中心动作不变。对照新原图1.0、旧原图0.9931、Pause0.5842、技能详情0.5216属于OpenCV定位证据；最终生产Service/门禁/执行器实际回放两原图各连续送达两次原中心输入，首笔后置确认才续下一笔，遮掉标记、黑白空帧、Pause及详情页均零输入。七用例已接原recorded-input-contract，recognition24+app15共39入口complete=true/device_actions=false；receipt在.local/rescue-marker-contracts-261020-c2/，不是39轮游戏或救人成功证明。

首个基线命令从仓库根而非next目录运行导致空JSON路径错误，保留marker-baseline.log；修正cwd后marker-baseline-c2.log才是已复现的生产识别失败证据，不混称两者。

首候选c1完成编译但在healing-effect-contract报COMPILE_IMAGE_SCAN:Back0:invalid map<K, T> key，原因是新增语义项未接location_probes.hpp的显式资源表，且共享资源误带locale键。失败回执和日志保持；c2补同一资源表登记、共享查表及编译素材闭包，重新使用全新隔离根验证，没有修改失败断言或生产门禁。

原build.py/package_functional/manage_service于18:42完成正式构建、打包、预检和部署；候选.local/rescue-marker-candidate-261020，EXE1b71deb345ea58aac2f4c6281b0dac066236c7c73afc3d1ecdfffd80bfff740d，输入49479bba4f93c0bd84579615707a07d361cd4bc265ce4282643594e9430c8a3c/1331，与39回归同源，8份验证二进制哈希未变。只关闭身份确认B288服务，旧维护回执SHA及13字段保持，27份权威哈希保持，MuMu实例2/Android Clash/tun0 UP实核，未改VPN或设备设置。

新服务E65BF7A4-E24A-46D5-BB1F-A3798B4188F9/PID50592/start_filetime134369665557431172，原17654/data-root不变。单次提交giant-189-rescue-marker-20261020-3e575bc2b41747bc80ae86662f520f7d，只续189；账本在.local/rescue-marker-resume-261020/，基数11，不加入旧B288维护半轮或失败半轮。实际Running/run1自然触发救人：原帧12的旧标记0.65515、新外观0.97808，107接受epoch6；原帧14旧0.65353、新0.98499，129接受epoch8、133确认后置；所有输入仍为原中心。210及236分别确认原再起Accept/Continue，随后正常战斗推进至917/原帧109，新段恢复事件0、仍11/200。证明此次缺失外观已在自然实机输入中越过，不冒称救人成功、无所有潜在故障或已完成整轮。现场和live-rescue-revival-events.json/resume-confirmation.json已保存，未commit/push。

## 当前结论

2026-10-20 14:26巡检，0BDB3888-A408-4044-9EDD-B917D404D33F/run1已Failed/EVENT_DEPTH_LIMIT，busy=false、quiescent=true。根请求giant-189-healing-reward-20261020-ebad69a04d124fdb84a6c169a142d268停止，repeat.completed_cycles=0；该轮result_saved=true但仅1业务单元，不是成功。原基数11不变，同一200目标仍11/200、剩189。

现场、进程创建时间、EXE SHA及正式data-root与healing-reward-resume-261020/resume-ledger.json一致。原始证据只读复制到next/.local/hr-check-261020-1426/，未写测试数据到正式目录。新鲜预览由原Application.capture_device入口取得，未提交游戏输入。

## 已证实链路

- SelectedHandler4是原with_boot_recovery注册的wvd-party-death处理器，实际为dismiss_party_death，不是新奖励交接处理器。救人中心输入epoch13、14分别有confirmed回执，epoch15没有确认。
- 事件1449为SelectedHandler4_Dismiss@await的CONTINUOUS_EXCEPTION_TIMEOUT主动游戏重启；原诊断帧132见diagnostics/1.png，为单人救人画面，不能用其推断真实多人死亡或治疗成功。
- 事件1655、1835、2036又分别在ReconnectBoot_Poll、ReconnectBoot_Title@await、ReconnectBoot_Poll超时并主动游戏重启。四个diagnostic.application_restart代表四次重启，不把其它恢复事件重复算重启。
- diagnostics/3.png为启动加载100%，2/4为黑屏；2048保存EVENT_DEPTH_LIMIT失败帧，最终执行栈已有嵌套ReconnectBoot。FlowExecutor::check_events在stack_.size()达到8时按现有保护失败，不能提高深度上限掩盖原因。
- 结束时epoch15的result_wait_ns=265525889200，其中event_and_read_pause_ns=209344236600；epoch36的result_wait_ns=85777546000，其中event_and_read_pause_ns=72954686400。这不是单个连续等待时长，更不能据此声称硬件故障。
- 最新live-now.png是启动须知页，live-capture.json记录MUMU_IPC、900x1600、ADB16448及246ms帧龄，前台为原游戏。该新帧不同于诊断黑屏，因此未认定官方完全无交互、未要求手机重置，也未将故障归因于MuMu历史转储。

## 原账与安全边界

stop-pending.json及原result.json保存两笔完整13字段：epoch15/basis80/SelectedHandler4_Dismiss/attempts1，epoch36/basis419/ReconnectBoot_Title/attempts4；均Sent、delivery_unknown=false，不是Unknown，但仍无真实确认回执，不能伪造结清或重放。原execution-events.jsonl、action-timing.jsonl、diagnostics.jsonl、recognition-memory.log、memory-lifecycle.json、run.json及全部诊断原图保留。

原始救人页输入后为何未获得后置，以及后续启动为何连续超时，尚未证实。没有新增点击、恢复T0取消、修改等待/ROI/阈值/深度、清pending、服务部署、续跑、commit或push。只读审查过party_death.cpp、native_recognizers.cpp、boot.cpp和flow_executor_events.cpp的真实调用链，但未以猜测修改生产代码。

此前代理把保留现场误解释为停止主动排查，14:36至17:16仅重复只读核对。用户未发人工暂停，该解释错误；2026-10-20 17:20起继续隔离复现、修复及原流程部署准备。不能把这段无进展巡检表述为持续修复，也不能把自设等待条件当作用户保护停点。

## 恢复嵌套修复

已核实运行时代码缺陷：父救人输入没有menu retry，导致全栈menu_only为false；Boot自身可重选的Title也因此无法按既有菜单中断语义重新检查。原逻辑只检查最早事件栈是否全无pending，父救人pending阻止回收，每次已核实应用重启便追加相同Boot。它解释恢复栈增长，但不解释救人画面最初为何未推进。

只修改原FlowExecutor::retry_observation：复用原restartable-menu判断，定位同owner0/同规则ID/同handler_definition的Boot，只有该Boot及其子调用全部无pending或按既有规则可重选且非Unknown时，才将其菜单输入记录为interrupted_by_application_restart、回收这一恢复段并在原位置重新进入Boot。父业务/救人输入不在该范围，完整13字段保持；不可重选或Unknown输入保留原路径与保护，不修改深度上限、重启等待或游戏识别。

扩展现有test_native_flow的restart-during-boot入口，没有新增生产旁路。旧生产代码在新增父账用例明确失败REPEATED_BOOT_STACK_GREW:confirmed；原四个用例仍通过，基线二进制及SHA在.local/rescue-boot-fix-261020-1720/保留。修复后连续9次恢复保持最多两帧执行栈，父账逐次相等；真实结果条件成立才允许完成，无结果则保留原账取消。新Unknown夹具最初错误预期也应执行9次Boot，实际生产保护提前报INPUT_BLOCKED_BY_UNKNOWN_DELIVERY；保留candidate.log失败，只把新夹具预期改为一次恢复、无Boot额外输入、原账保持，未放宽生产门禁。

独立.local/rescue-boot-contracts-261020-c2/中runtime19及app15共34入口全部通过，complete=true/device_actions=false；产品输入a66b25563ff1f7ec29eaebdab4a1ea7d8c2867c11cf0f55bc7da585b083f7f4a/1324。包含Unknown祖先重连输入、跨其它事件处理器阻断、结果清理未确认、低意志力、事件停止、Coordinator实例退出等原保护。这是34个离线入口，不是34轮实机。

重新逐份核原11份Completed/三单元/result_saved/quiescent及其哈希，0BDB/run1仅1单元失败排除；新completed-audit.json记录总200、已11、剩189，旧审计仅引用不重复相加。两笔原Sent非Unknown输入及全部原日志/诊断图另存failed-*，27份正式配置/流程哈希保持。新鲜pre-deploy-live.png仍是启动须知页，不冒称官方完全无交互或需要手机重置。

## 部署与实机进展

原build.py/package_functional.py完成构建打包，manage_service.ps1的Validate和Deploy通过；只关闭核实的0BDB3888/PID53404，旧失败回执哈希部署后保持，未伪确认旧输入。新候选.local/rescue-boot-candidate-261020的EXE为a929eb1797123ac5c92aad3f6af905dd6a538e7e770cbaee078389b12d22d613，产品输入与34入口回执相同，验证二进制部署后哈希也保持。当前B28814C5-AE1A-4451-BB70-3A09680A1C69/PID47384/start_filetime134369630050982114，原17654/data-root/模拟器2/VPN保持。

同一200目标只续189，根giant-189-rescue-boot-20261020-1a405dc2c14e47b8b50616217a146d69，first_run_id1、基数11。新ledger先保存再单次提交，preparing不重发。实核Running/run1，Boot_Attention事件21和Boot_Title154 confirmed；新帧进入巨人战斗并有11笔已确认战斗输入。随后自然出现一次死亡过渡：救人1251/1262/1283、原再起Accept1323/Continue1348及后续原返城/战斗输入均confirmed，继续战斗/宝箱，未触发主动游戏重启。事件2238、原帧315时新段recovery0、整轮尚未完成，累计仍11/200；不是200完成，也不冒称原画面冻结的内部原因已根治。

新账目、完整pending/新原帧、live-rescue-revival-events.json及resume-confirmation.json在.local/rescue-boot-resume-261020/。原停止图另经既有Service/门禁单帧回放，Dismiss/AutoOff/CloseCharacter均NoHit，后端0输入，故不借画面补点X或中心；该回放仅证明识别/门禁，旧识别二进制未当作新运行时验证。初次误用冻结产品pack出现BUNDLE_DUPLICATE_FILE、随后fixture-pack路径传错的诊断夹具失败均保留；改用原run_contracts物化的全新独立测试pack后通过，没有修改生产门禁或素材阈值。

监控继续每10分钟，正常安静；普通故障保现场后主动隔离诊断/修复，不再以重复只读核同一停止状态代替工作。真实人工暂停、低意志力、多人死亡、Unknown等仍禁止现场确认/取消/返回/重启/续跑，但不阻断无设备副作用的离线分析。200完成暂停，不新增批次。未commit/push；MuMu内部根因及RESOURCE_UNRESOLVED保持未决。
