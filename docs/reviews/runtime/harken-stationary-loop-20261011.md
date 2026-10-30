# 返哈肯静止交接循环

## 现场事实

- 09:10小时巡检：正式实例0E0B77E8-7599-4886-84EF-2AD5C6415F71/PID63136，EXE3991d52967056ccadb470aab7135898a3f5292b457ccf85f144ef830bc0b77e9；当前96轮请求giant-96-return-entry-20261011-6b5f52e236af420594589080391f1f5b成功18轮，run19失败。18份回执均Completed、completed_cycles=1、quiescent/result_saved=true；加原4轮，总成功22，剩余78。
- 原因OBSERVATION_RECOVERY_EXHAUSTED:ADB_TIMEOUT，quiescent/details_complete/result_saved=true，pending=null、secondary_errors为空。终态设备观察为connected/application_running/application_foreground=true、instance_exited=false。
- run19先有真实实例恢复：事件3431记录instance_restarted_in_window=true、application_restarted_menu_reclassified，持续33910ms，原退出证明保留。随后返城重复回到同一哈肯正面，最终原帧diagnostics/3.png为正常迷宫/哈肯场景，不是死亡、付款或未知送达现场。
- 最后读取只获4ms命令期限，21ms执行后被归为ADB_TIMEOUT；读取恢复仅34ms/1次失败。它发生在整段期限边缘，不能描述为连续ADB故障60秒，也不能据此认定模拟器再次闪退。
- 原result/events/action-timing/diagnostics/run/memory-lifecycle及原帧复制至next/.local/observation-deadline-20261011/before-*，正式证据不改。

## 控制链根因

- navigation.auto_route(dungFlag)的Moving包含Stopped；静止检测三秒后命中StoppedExit，交给return_to_bounty_city的StoppedExit，再到RecoverReturn/ResumeReturn/Entry，重新创建返回导航。
- run19出现122次ReturnHarken_Entry、121次RecoverReturn；同一返哈肯Choose末段输入均832,287。每次重新进入都丢失原Poll无进展窗口，静止被当正常插入，未进入已有StalledExit。反复输入不是业务进展，也不证明已到楼层菜单。
- 旧src/script.py的StateMoving_CheckStop把静止交回状态识别；退出仍另找归还/楼层/城市，不把静止当返城成功。原生调用者把这类返回反复接回同一导航而没有保留无进展等待，是本次需修的具体链。

## 修复与验证边界

- 只对返哈肯移除Moving的正常Stopped交接及其孤立观察节点，保留同一次Poll。实际迷宫位置进展仍按既有region_changed续期；楼层/祝福/郊外、遭遇、无路线继续按现有出口优先处理。无进展期限到达沿已有StalledExit/navigation.input_no_progress及原生命周期策略，不改60秒期限、ROI、阈值、救人/Unknown保护。
- 标记/宝箱搜索的Stopped语义不变；不新增识别素材、处理器或旁路执行器。总期限边缘错误仅为后果，不扩大总期限掩盖死循环。
- 现有harken-no-route合同补同一静止帧等待后正常到楼层与持续无进展的对照；仅夹具缩短等待来验证生产控制链，生产60秒独立断言。旧生产实现同夹具exit=1/HARKEN_STATIONARY_HANDOFF_LOOP:HANDOFF_REQUIRES_CALLER:stopped；最终exit=0，两个新场景各只有一次导航输入，既有无路线/恢复选路断言保留。before-regression.log与after-regression.log记录完整结果。
- 首次候选图验证拒绝COMPILE_ORPHAN_NODE:Stopped，失败回执保存在hks-261011/receipt.json；对应观察节点随该目标出口一起调整后，最终app/recognition/runtime共47入口complete=true/device_actions=false，回执hks2-261011/receipt.json。补充harken-no-route实际执行二进制SHA256 c472ec34269b4c0d83c2f987d7b33ee0fe37f233d6fc1b1e36a97d882a418c8b与模块回归一致，不把它误写成实机验证。
- 完整构建/冻结包输入d854762d0e61347432f3da388005a177b1ce98273f3b31dcda93cdba4234f557/1299项，与最终回归一致。已在原端口17654、原data-root部署；EXE602d10c33b82c58038be492beb8e5b90527f25c592399b5971d4a2f1a239c66d，实例1469931A-0028-4A3D-816E-7134FDDE2AFE/PID34172/start_filetime134361565934281383。
- 当前78轮请求giant-78-harken-stall-20261011-1a079d2535ba468db18d878fac291910确已submitted/run1/Running。实机同次Moving进入StalledExit/navigation.input_no_progress，不再反复点击；但两次既有生命周期恢复后仍回到哈肯实体前，楼层菜单未出现，业务未继续。本轮由代理主动停止，UserStopped/STOP_REQUESTED/quiescent=true，成功0轮；不是用户新暂停，不能冒称恢复成功。
- generation1/2均navigation.input_no_progress且无unresolved_inputs；停止碰到generation3 Boot_Title等待，原Sent菜单输入attempts3/delivery_unknown=false仍在sessions.unresolved_inputs，不能凭顶层pending=null称完全没有原回执。完整记录intermediate-result.json/intermediate-events.jsonl，未确认旧输入成功、未再输入或启动循环。
- 停后通过现有device/capture只读取新预览，MUMU_IPC/900x1600、foreground_application为游戏、connection_generation1；原始stopped-preview.png仍为哈肯实体前，tun0 UP/LOWER_UP。不能用仅图像判断物理冻结，也不能猜哈肯实体的交互坐标。已询问用户该现场需要点实体还是快捷按钮应直接进入菜单；待合法入口确认/修复后才续剩余78轮。
- 当前总成功22/100、剩余78；正式profile SHA/revision不变，保存流程未改。新78轮不能计入成功，后台保持空闲，不再自动制造同类重启。小时巡检需读本报告及ledger的hold_reason，未取得新现场/合法动作证据前只读检查，不自动抵消此修复停点。
- 内存仍RESOURCE_UNRESOLVED，保留趋势日志，不新增WPR/ETL/堆采集，不提交/推送。
