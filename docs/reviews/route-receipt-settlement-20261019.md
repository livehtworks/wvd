# 路线完成回执与菜单误绑定

## 同一100轮的实际中断

本次统计从C9F0EB83实例的原请求1352c5ab-88c1-49ca-8b89-26627c289aa4开始，沿四次续跑的原账逐份核对81份终态：76次Completed，5次未完成。更早的100轮和隔离测试不混入。

| 实例／尝试 | 终态 | 原因 |
| --- | --- | --- |
| C9F0EB83/run1 | UserStopped | 为LV3修复主动停止 |
| 43E2B469/run16 | UserStopped | 按用户要求维护停止、修复恢复到哈肯菜单的交接 |
| 87BA907F/run2 | UserStopped | 返城异常已触发恢复后主动停止维护，不掩盖此前返城异常 |
| EED629D7/run51 | Failed | quest.bounty_transfer_unconfirmed，已准备跳轮的续接遗漏 |
| A93DE32D/run11 | Failed | 本次FLOW_TOTAL_DEADLINE，任务点已确认仍等菜单，反复重启 |

同范围有14个去重的bound_game.restart主动重启请求，本次run11占10个；另记录16个应用重启恢复窗口，其中包含主动重启，不能把14与16相加。统计按实例/run/started_at_ns去重，不把同一恢复窗口中的ADB重试当成多次重启。完整清单在next/.local/pt-261019/batch-audit.json，76次成功逐份回执可追溯。三次维护停止也算中断，不能只报告两次自动失败来淡化问题。

## 这次真正卡在哪里

- 正式A93DE32D-74E4-47C7-BB19-9C7E7F1E9CD1/run11，目标战斗已结算，unit=1、phase=FirstRoute、task_step=2、unit_matches=true；未决输入为空。
- 事件3343进入HarkenArrived，3346在frame321保存第二个target_completed；最后任务点已由原识别和确认职责接管，不是靠点击次数推定到达。
- 随后经过恢复尝试，3480在frame339确认healing_attempt_finished；3496副本子流程退出，3498父流程进入FirstPoints。现场变为迷宫HUD和哈肯实体，不再是地图/楼层菜单。原图保存于pt-261019/diagnostics/及current.png；未证明游戏冻结。
- FirstPoints业务守卫task_step==2成立，但唯一正常后继FirstRouteDone还要求map或harken_floor_menu。后继不命中，诊断successor_not_confirmed；虽然业务路线已经完成，上层仍等瞬时页面。
- 事件3594首次因此在60463ms请求重启；同一个FirstPoints问题重复10次，恢复没有改变已完成任务点和错误菜单要求，最终耗尽整轮总期限。最终reason仍是FLOW_TOTAL_DEADLINE，不改写为卡死或设备退出。

源码链：dungeon_route的Confirm1/target_completed → healing退出 → Finished/Terminal → bounty_cycle的FirstPoints → FirstRouteDone不必要的菜单条件。旧src/script.py的StateDungeon在目标列表完成后break，蝎女调用者直接转向公会返城/提交，不要求原地图菜单继续保持显示；不能照搬其中[1,1]盲点来修。

## 当前修复

仅对last.harken_arrival的悬赏路线，First/SecondRouteDone改为核对已有本轮phase、unit_matches、task_step==路线长度的业务回执，登记bounty_route_completed。原BountyCycle::route_completed仍独立核对phase/unit/点数，不能凭哈肯画面或缺一个任务点放行。

路线登记完成后进入现有return_to_bounty_city。它再识别当前页面、处理哈肯/迷宫/遭遇并确认回城；新输入仍使用新帧与原门禁，不拿旧菜单或旧到达回执取得输入资格。Jier离开哈肯及普通非哈肯地图路线条件保持。没有新处理器、盲点、全模板扫描、重试次数限制、超时延长、ROI或阈值变更。

## 验证和交付状态

在既有完整giant_linkage生产工厂用例中加入：第二点确认后菜单消失回HUD，以及菜单被新战斗替换。要求只登记路线一次、只打原目标一次、复用返城/遭遇职责完成三段和一轮，不借重启通过。此前跳轮恢复、救人/宝箱、途中战斗、业务单元/跨城未决反例保留。模块回归、正式构建及实机结果完成后据实补记。

- 最终C2完整工厂12种正例及2种保护反例全部通过；新增两个边界均target_battles=1、route登记/返回各一次，遇敌正例return_battles=1，最终cycle=1、pending=0。使用正式profile/保存流程的只读副本，语义页面和设备隔离；不是14轮游戏。
- 首份after-fix夹具的菜单消失场景已通过，但遇敌场景错误模拟战后直接进入楼层菜单，停在现有combat_phase不支持的该合成出口。按本次原帧的迷宫HUD顺序校正show，保留after-fix失败输出与C1候选、不修改生产战斗出口、不削弱完成/目标次数/返城次数断言。C1的19项模块结果仍保留，但未放行该候选。
- 最终recognition C2的19入口complete=true，源码输入fb13ea5994b3dd43b50d5ca128295cc4f28daf972b2564d3ca7878d5e7731688/1315，与最终完整工厂测试二进制及冻结候选一致。此前未变化的Unknown输入等保护仍引用bt-261019的三组定向回归，不冒称本轮改过内核。
- 原build.py/package_functional/manage_service构建、冻结并部署next/.local/pt2-candidate-261019，EXE3bfc3b14c7bdda20a3ab1e48bbb7e65c36c0fb3149c416b7f4ddf07361493d95。旧A93DE32D/PID38248经身份核对正常关闭；新F18FB48A-921E-421E-BF6C-EE4A59572E85/PID32716/start_filetime134368691854080210，仍为17654及原data-root。27份profile/workflows哈希未变。
- 新请求giant-24-route-receipt-20261019-44522571959e4c849fdb083ccded2dc0，GiantBounty/zh-Hant/repeat_count24，基数76。15:49已实核新run1保存Completed、三业务单元、quiescent=true、空未决、恢复/重启事件0；战斗/返城/交账/住宿完整结束，末尾事件4229，run2已实际Running推进战斗。只计76+当前completed_cycles，目前77/100，失败run11不计；继续跟踪剩余批次至实际终态，不把一轮完成写成100轮或连续无故障100。证据full-round-confirmation.json及completed-run1-result.json。

## 实机批次最终结果

- 2026-10-19 18:14巡检确认同一目标累计100/100。原四段76份保存Completed重新逐份核对；本段run1–24全部保存Completed、每轮三业务单元、quiescent=true、无未决输入。105份终态共100成功、5中断（此前三次维护主动停止、两次自动失败）；失败和维护轮不计成功。本段没有新的批次停止，不是重新开100，也不是依靠accepted计数。
- 最终run24 Completed/Terminal、busy=false、repeat.active=false、repeat.state=completed、completed_cycles=target_cycles=24；服务身份、EXE哈希与部署值一致，27份profile/workflows哈希仍与启动前一致。没有开启下一批、定时检查或外部守护，也未commit/push。最终证据next/.local/pt-261019/completed-batch-audit.json、completed-run24-result.json、completed-current.json、completed-service.json；原启动账resume-ledger.json的76基数不重复加入当前24。
- 本段run12发生一次独立恢复：原HarkenExit_Floor点击归还(453,1051)后菜单消失，诊断原帧为无HUD哈肯展示，约60秒未确认返城，事件3809请求bound_game.restart；不是父FirstPoints还在等待旧菜单，也没有证据证明这次是模板漏识别或官方卡死。原有恢复随后经标题、公会接回，run12最终完整交账和住宿成功，不重复计数。原帧、输入与日志在return-run12/；未按静态画面擅自新增点位或处理器，底层未推进根因仍未归因。
- 本段run18在旅店入口记录CAPTURE_METADATA_NOT_READY→ADB_TRANSPORT_FAILED→DEVICE_RECONNECT_WAIT，约20829ms后报告application_restarted_menu_reclassified；input_replayed=false、无实例重启证明，随后接回旅店并完整完成。原证据transport-run18/；不能将应用恢复回执解释成已证明模拟器崩溃或已查明底层连接失效原因。
- 本段九条恢复事件属于两个窗口，两窗口回执均报告应用重启；其中一次是显式bound_game.restart请求，另一次由设备连接恢复进入。加此前14次主动重启请求/16个应用重启恢复窗口，同一目标合计15次主动请求、18个应用重启恢复窗口，两类包含关系不相加为33。实际跑满100不等于连续无故障100；本次修复后24轮无批次中断，但有上述两次途中恢复，仍不宣称全部稳定性问题闭环。

未commit/push、未恢复定时检查。原失败、14次重启及76次成功账目不覆盖；归因明确的是父流程对旧菜单的不必要依赖，不把底层ADB closed、旧无HUD停滞或历史内存增长冒称一起修好。回滚须停止匹配请求并核静止、用原管理器关闭新实例，再把保留的bt候选部署至原端口/data-root；权威文件哈希、源码前态和人工回滚材料均保留，不提供双服务自动回退。
