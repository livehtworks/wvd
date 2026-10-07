# 网络弹窗误判与恢复返回修复

## 边界

用户要求修复后commit/push/部署，明确先不开循环。本轮不连接、点击、重启游戏或模拟器，不更改正式profile、VPN、任务覆盖或历史业务结果。candidate130的失败证据保留，见[首轮停止核查](giant130-first-round-stop-20261007.md)。

## 修改

- 战斗的“禁止盲点”与“判定未知技能详情”分开：前者仍可用通用确认/关闭按钮挡住输入；后者改为战斗状态加技能独有的明细锚点，不再凭任一通用金色按钮就进入UnownedDetail。不改ROI和阈值，不引入每帧扫描全部异常。
- UnownedDetail声明当前错误条件及消失后回Entry的只读边。生产执行器执行条件失败节点前处理已有异常，再重新观察错误条件；条件NoHit且无待确认输入才走声明的返回边。Error仍失败，Hit仍报原错误，无条件失败语义不变，不重放输入、不消费技能、不切Auto。
- 编译器和原生定义所有者同步约束：带正常返回边的条件恢复节点必须属于本子流程，不再当跨作用域共享终止出口。失败节点guard与返回边成对校验，缺一不可。
- 本轮没有修改持久数据结构、公共流程副本或语言映射。技能确认模板仍是位置识别，不能单独充当技能场景证明；真正施放仍要求已选角色和详情证据。

## 验证范围

仅运行与本缺陷直接关联的生产代码路径：真实frame210网络重试、最终连接中帧、frame195真技能详情通过正式识别Service；生产FlowExecutor验证事件返回后消失/仍存在/识别Error/无条件失败，并保留未确认输入保护回归；生产战斗和巨人图验证条件失败节点的作用域、语言映射及合法返回边。不用实机长循环代替确定性缺陷验收。

验证已通过：

- test_support_selection --popup-frames：生产Service复现通用确认模板误命中网络按钮，但新的UnownedDetail分类为NoHit；连接中为NoHit，真技能详情为Hit。原帧未加工，证据位于next/.local/support-check-593040840727200/evidence.json。
- test_native_flow --recovery-recheck：网络事件处理一次后错误消失正常返回、仍存在保留原失败、识别Error保留错误、无条件失败保持原行为；不完整guard/返回边组合拒绝。全程无输入。
- 原--critical-recovery、--selection-race回归通过，未确认输入不重放，角色/页面变化重新观察，不更改保护语义。
- test_native_author --combat-open-repair通过；--giant-bounty只读正式profile，验证巨人完整编译/繁中覆盖、子作用域及共享蝎女图通过，未执行游戏。
- automationd、test_native_flow、test_native_author、test_support_selection均构建完成。首次命令误用不存在的test_flow_executor目标，产品已构建但该命令失败；改为CMake实际test_native_flow目标后成功，不记为首次通过。

日志位于next/.local/popup131-*.log（build、check-build、real-frames、recheck、protected-input、selection、combat-open、giant-graph）。实际部署身份在执行后补记。长期内存结论仍为RESOURCE_UNRESOLVED，本次不增加内存采集或擅自重开30轮。
