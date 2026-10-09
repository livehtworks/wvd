# 分支重选与业务续接修复

## 范围

用户在只读审查后明确要求修复并恢复循环。基线为candidate135停止现场，累计58/100、续批2/44、run3失败；原故障证据及链路审查见[审查报告](flow-dispatch-chain-audit-20261009.md)。不把有限验证等同几百轮或几千轮稳定性证明。

## 实现

- 执行器对尚未执行的节点统一保存局部选择来源，不再只支持Input/Observe/Route。视觉条件或业务守卫失效时先重选同层候选，尚未发送输入不启动错误恢复；保留原分派期限、返回目标及业务状态。
- OperationState新增NotApplicable，明确表示尚未产生效果。WVD业务确认及未知跳轮前置失效采用此结果。Waiting保持进行中语义并锁住重选，不重放可能已经开始的操作。
- 到期正常场景复查及60秒重启前检查支持带新鲜正面条件的业务确认、子调用等节点。候选是同层可替代分支，不因为列表中存在动作节点而跳过后面的合法场景。过期Poll的正常出口复核合并到该路径；其无进展失败边、已知进度续期及输入保护保留。
- 战斗角色子调用明确带combat_active条件。条件失效可回战斗、迷宫、宝箱、再起等同层判断，不靠无条件子调用证明已知场景。
- 事件恢复先取得原输入的当前结果再判等待超时，避免刚刚已出现结果却先被旧期限拦截；未确认输入依然不能清除重放。
- 重启或宝箱插入战斗造成目标身份丢失时，继续使用正常战斗方案处理当前战斗。之后以新鲜迷宫证据退役未确认的目标尝试，再重新导航目标点。退役不增加task_step、不记目标完成；再起不能消除identity_lost。目标是否完成仍由重新到达/目标战及报告链确认。
- 日志新增last_selection，记录来源、候选、选中节点、帧号、代次、输入epoch及有界识别证据。单候选明细超过2048字节显式标记省略，最多24条且限制总明细增长；不复制图片。原动作时间、异常截图及业务回执保留。
- 实机136补充暴露战斗角色入口缺少无菜单等待：敌人模型退场但行动条仍在时，原Entry只能等菜单或结束锚点，因而累计未知异常。新增AnimationWait，以正向战斗证据、无阻挡弹窗、菜单未出现及input_clear限定；同层持续检查结束/菜单/异常，行动条区域真实变化可刷新进度期限。它不发送Auto，也不凭等待认定战斗胜利；静止超期仍进入既有战斗无进展恢复。

## 幂等边界

正常页面变化不消耗业务完成次数；未执行候选撤回时撤销其临时命中计数。已发送输入不通过换分支绕过确认。业务事件继续使用原operation ID去重，不以每张截图或新连接代次生成新的业务完成事实。

无限循环由既有单一循环所有者逐轮创建、收尾、释放，不在单个战斗调用内无限延长运行预算。持续未知仍按既定60秒设备/游戏恢复；缺资源、不可确认的持久副作用、用户停止和禁止宝石消费等边界不取消。

## 验收与部署状态

最终Release构建通过。selection-race、linkage-closure、critical-recovery、late-receipt、recovery-recheck针对性入口通过；巨人正式工厂+实际保存公共定义的8种隔离设备场景通过，包含战斗中重启和宝箱后新战斗的重新导航。日志和逐项结果位于`next/.local/flow-chain-audit-20261009/final-*.log`及`isolated-final/`。一次新增测试夹具误用了非法business_guard节点形状，已按生产Route契约修正后重新通过，没有放宽生产校验。

本次验收覆盖：现有selection-race增加业务确认/RegisteredOperation/业务守卫/Call及已开始操作；既有过期Poll链增加确认/Call出口；巨人正式工厂及业务状态链增加战斗中重启后重新导航，并要求未归属战斗不得直接推进任务点。像素/设备隔离的这些检查不证明实机成功，部署后另记录实际循环。

## 正式运行

- candidate136已部署原17654/原data，PID54532，服务27CB6108-A6A9-4D58-9834-1EDD4FC6B343。旧135/PID27016经管理器正常退出，未关闭模拟器；EXE SHA256为80729c85e181322f3737e496ccab02bc6f9cff6af63d243bccf96d99374520f8。
- 正式profile SHA256仍为3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C；实例2、繁中、现有巨人方案及AUTO_START_CLASH=true不变。
- 从正式runs/start入口续开剩余42轮，请求fe969c62-8188-48d6-ab23-7654f9085a44，协调器86DC3F54-67A3-4B25-BF87-AE4A0D6C2DC7。确实进入真实战斗，但首轮随后暴露上述无菜单等待缺口，最终UserStopped、busy=false/quiescent=true、0/42。停止确认前日志已记录CONTINUOUS_EXCEPTION_TIMEOUT及设备恢复，不能称为已阻止重启。累计仍为58/100。
- 初始EnsureVpn和StartApplication均观察到已就绪并确认；新鲜留存帧显示真实巨人战斗、面具Lv2技能选择，执行节点持续推进。原58轮计数保留，新批未完成轮不计数。内存、耗时、识别、业务回执和历史截图继续沿原开关采集。
- 长期几百/几千轮稳定性和MuMu显卡驱动故障仍需真实运行证据，未宣称已证明；也没有移除缺素材、多人死亡人工处理、宝石购买禁令或未知输入保护。
- 136补充证据：run1 events中的Actor_Entry持续successor_not_confirmed；历史帧f541已独立保存为`next/.local/flow-chain-audit-20261009/battle-wait-136.jpg`，显示无敌人模型、仍有行动条与六人状态卡、无技能菜单。该帧只证明等待外观，不证明服务器已结算胜利，不将136算作完整一轮通过。

### candidate137

- 新增combat-animation-wait针对性入口通过：正式保存方案的隔离副本、正式角色工厂/公共流程/执行器，已知无菜单等待超过1.2秒仍正常、异常计时未启动、输入为零；转结束证据后Completed且仍零输入。场景识别使用受控端口，不冒充实机。随后巨人8种链路场景重新通过，记录在combat-animation-wait.log、giant137.log与isolated-final/。
- Release及资源冻结打包通过。137已部署原17654/原data，旧136/PID54532正常退出；当前PID39432、服务362BDC71-AB69-4933-87F0-46DD8E589020；EXE SHA256为17B9A336FAB08CE71893D3F8C822DBA36AEF9B35F9914AE15BB4333B54E6D224，正式profile哈希不变。
- 正式入口再次请求剩余42轮：40b98d12-ba2e-4452-adba-9b74bd93022e。实际运行与首轮交接另行记录，不以已受理代替完成。
- 137实际协调器81B098A6-C6E0-40E0-81B0-AAF831931035/run1：EnsureVpn确认vpn_ready=true，正式StartApplication拉起游戏，经标题和黑屏加载后回到现场。seq1375实际进入Task_FirstDungeon_Battle_Actor_AnimationWait；随后迷宫得到确认，seq1441–1457完成InspectReturnCity的ReturnHarken分支，Moving时wait_state=normal、continuous_exception=false，最终Retreated/Terminal。该段没有application_restart事件；它证明此次战斗等待能交回返程，不等同已完成整轮或长期稳定。
- 新任务周期进入真实巨人战斗后，seq3608 AnimationWait→3609 Prepare→3612 Select4→Skill4输入，实际证明菜单恢复会继续既定技能。循环计数由Application::watch_task_session收口：已保存、静止、Completed、三段业务结算、本轮completed_cycles=1且报告/住宿无待确认才计数，再用session+下一轮编号生成确定请求ID；无限模式只省略总次数限制，不另起执行路径。
- 137第1轮实际Completed，API记录804.3256802秒（含重新启动游戏、黑屏加载、旧现场收敛，不能当稳定单轮耗时）。正式result.json确认completed_business_units=3、task_step=2、bounty_cycle.completed_cycles=1/reports_remaining=0、inn_rest_completed=true、inn_payment_pending=false、bounty_report_pending=false、pending_inputs=[]、details_complete/result_saved/quiescent均true、storage_error为空、secondary_errors=[]。本周期200G住宿提交1次，未使用宝石。
- 工具自动续入run2，Running、repeat.active=true、completed_cycles=1/target_cycles=42，请求仍40b98d12-ba2e-4452-adba-9b74bd93022e，未人工再次启动。原58轮加本轮为59/100；136失败尝试未计入。保持本批继续运行，未开启第二套循环。当前记录只证明本轮完整收尾及自动续轮，不能据此承诺无限运行无异常或已完成几百/几千轮。
