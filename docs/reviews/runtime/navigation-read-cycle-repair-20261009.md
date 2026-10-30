# 第21轮导航空转修复

用户要求核查无法连续完成100轮的问题。此前32轮、第二批实际4轮、candidate133本批20轮已完成，累计56；第21轮未提交/住宿，后续续跑44轮，不计该失败轮完成。

## 现场事实

candidate133/PID39060，协调器50CEB64B-BD6C-47E4-AEB8-19EBC0697E49，第21轮1768.749989秒后Failed/FLOW_INVOCATION_TIMEOUT:Task_FirstDungeon_Entry，quiescent/details_complete=true，secondary_errors为空。task_step=1、combats=1、bounty_reports=0、inn_rests=0。

实际无路线提示命中Route1_Done并进入StoppedExit各751次，随后返回Dispatch/Resume，同一返程步骤未推进。停止截图diagnostics/2.png明确显示繁中“找不到前往目的地的路線”。该图frame804/action_epoch234，age_at_submit_ms=213478，即保存时已是213秒前的画面。

原始证据：next/.local/c11-flow-product/data/runs/50CEB64B-BD6C-47E4-AEB8-19EBC0697E49/21。

## 根因与实现

1. FlowExecutor只在存在Overlay规则的无等待控制链上每100ms刷新观察；导航规则主要是Exception/Special，正向观察及子返回持续命中旧图且不经过Poll/Input时，可无限复用旧观察。现在全部只读观察周期到100ms即失效、下一次视觉判断重新采集。仍保留同周期的子结果交接，未修改点击期限/门禁或扩大异常扫描组。
2. auto_route(dungFlag)把无路线交给stopped端口，父层又回Dispatch重跑。现在旧标记提示先独立等待3秒，再以新画面决定继续或恢复；此等待两条后继均显式声明，提示消失不会卡在旧条件。返程输入后的真实无路线转navigation.harken_route_unavailable，经既有WVD恢复策略重新进入游戏及业务观察；不会冒充已到哈肯。
3. 标记/宝箱导航的无路线完成语义保持；返哈肯成功仍依赖哈肯楼层/加护/郊外等真实退场证据。

## 定向验证

- test_native_flow --positive-loop-frame：真实执行器在无Overlay的正向观察/子返回循环中重新取图并结束，非输入循环不能一直复用旧帧。
- --child-result-frame及--critical-recovery：同周期结果交接、失效身份重新取图、受保护输入不重放保持。
- test_native_author --harken-no-route：实际生产自动路线与执行器验证旧提示消失后只点击一次、持续旧提示及新输入后无路线进入恢复计划，无751次handoff空转。
- 完整巨人四种配置及共享蝎女编译通过；最终巨人2315节点，繁中依赖覆盖complete=true，foreign_ocr/unclassified/unresolved_foreign为空。隔离验证不代替以下实机结果。

## 实机发现的公共步骤交接缺口

candidate134续44轮的第1轮已成功发出返哈肯输入，随后页面进入哈肯楼层菜单，但此前选中的navigation-resume公共步骤只允许迷宫内点击继续；场景改变后没有只读返回出口，仍卡在SharedStep0_Author_resume并触发60秒恢复。此次续批没有Completed，正式停止后确认UserStopped/busy=false/quiescent=true，不计入已完成56轮。

实际楼层菜单证据：next/.local/c11-flow-product/data/runs/8C196357-2DAA-4343-B95F-12395B7F4C39/1/diagnostics/1.png。

修复：navigation-resume入口优先识别已结束的导航场景或不可继续状态，再决定是否点击；选中后、输入前页面变化也可只读返回。auto_route_post补齐繁中哈肯楼层、加护及郊外退场特征，资源冻结同时收集这些依赖。公共步骤返回不证明外层路线目标成功，父层仍需真实哈肯/退场证据。

- 既有--harken-no-route验证补上公共步骤调用前已到楼层菜单、选中后点击前变为楼层菜单，两种情况均完成交接且底层输入为零。
- 实机楼层菜单截图经生产Service、完整冻结包及真实OCR模型识别：auto_route_post=Hit、auto_route_moving=NoHit；不是假造场景结果。
- 源定义、打包定义、正式保存定义同步。正式更新通过revision比较交换，旧定义备份next/.local/navigation135-definition/before.json；新revision=7881d5cb45fd7179cbca79c5470fcd6c576e14399f08eeee32de54052d5faa4f，回读entry=entry/4节点。

## 最终部署与续跑

candidate135已部署原17654/原data，PID27016，服务6487D525-68F2-4D3E-99AB-2B14D06A1D08。正式profile哈希仍为3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C，没有改战斗方案/VPN配置。

续44轮请求aac68f12-8756-4c05-ba88-018e712b6e67，协调器30206DAC-2F58-42DF-BFDB-AABABB81E6EF。实机第1轮606.0024879秒Completed/details_complete=true，error_code为空、secondary_errors为空、crashes=0。先收敛原现场并提交/住宿，再完整跳轮、悬赏刷新、入本、巨人战斗、战后恢复、返哈肯、返城交报告及住宿，确认退出旅店。combats=1、bounty_reports=2、inn_rests=2；旧报告收尾未另算一轮，bounty_cycle.completed_cycles=1。本批1/44，累计57/100；repeat自动进入starting准备第2轮，不是人工再次提交。

启动及核对回执next/.local/giant135-44-20261009-aac68f12，其中first-cycle-summary.json为窄字段摘要，原始证据保留在正式data/runs/30206DAC-2F58-42DF-BFDB-AABABB81E6EF/1。保持debug和memory/performance/recognition，未新开重型跟踪。一次完整实机通过不证明100轮连续稳定或显卡故障已消除。

本轮只修改上述运行/导航链，未更改用户配置、驱动或素材。图形驱动故障及长期资源归因仍保留原开放结论。

## 续批第3轮再次重启的只读核查

本批前2轮Completed，累计58/100。第3轮在战斗中重启，最终432.6890403秒Failed/target.continuation_identity_unconfirmed，repeat inactive，busy=false/quiescent=true。该轮combats/reports/rests均为0，不能记为成功；工具PID27016仍在。本次仅核查和更新事实，未修代码或重新开循环。

第一触发点不是返哈肯。执行事件seq1216选中Task_FirstDungeon_Battle_Dungeon（combat.resume业务确认），seq1217开始step_guard_not_confirmed异常计时；一直等待到60178ms，触发CONTINUOUS_EXCEPTION_TIMEOUT/bound_game.restart。diagnostics/1.png保存时图龄13ms，显示柚奈行动、攻击按钮选中、正常战斗菜单及敌人；这次不是复用213秒旧帧。不能仅凭后来的截图断言最初候选为什么曾Hit，但已能证明新帧下旧确认条件不成立却没有重选战斗分支。

源码缺口：FlowExecutor候选选择的selection_origin及guard NoHit后的reconsider_uncommitted_selection仅覆盖Input/Observe/Route，未覆盖尚未执行的BusinessConfirm。encounter.cpp的Dungeon正是带场景条件的BusinessConfirm，因此场景在选中与确认之间改变时死等原节点。需要将未提交确认与已经开始执行的业务操作区分，再补同候选集重选；不能简单去掉60秒异常策略或把仍在战斗当作dungeon_resumed。

第二事件：MuMu实例2的shell.log.1第6568至6570行于19:08:44.387/388明确报告Graphics Driver crash/error901、crashModule=nvoglv64.dll/isGraphicsCrash=1，随后shell crash900。执行器先进入应用重启，模拟器其后崩溃；完整时序不支持把第一触发归因于驱动，也不证明应用重启必然导致驱动崩溃。此前“约2秒后”的口头估计不精确，战斗节点68.381秒段于19:08:45.553失败，60秒重启约在19:08:37，驱动退出约7秒后。

恢复实际执行RestartInstance、EnsureVpn、StartApplication，进入游戏后目标战恢复证据未确认，按target.continuation_identity_unconfirmed停止；不伪造目标已击杀，不提交未完成报告。停止后会话/OCR/识别器live均为0，PrivateUsage约143.6MiB，此轮没有工具内存溢出证据，长期资源归因仍开放。

原证据保留：next/.local/c11-flow-product/data/runs/30206DAC-2F58-42DF-BFDB-AABABB81E6EF/3；MuMu日志C:/soft/MuMu Player 12/vms/MuMuPlayer-15.0-2/logs/shell.log.1。本次确认的新代码缺口尚未修复，不沿用前一轮通过的结论。
