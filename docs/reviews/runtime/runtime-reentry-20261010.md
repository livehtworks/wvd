# 重入与页面交接修复

## 实机原因

- 2D62AB2D-9AA3-4444-8676-A418B4118111的run1重新启动时，停留在上次暂停遗留的爱丽丝详情。combat.turn的UnexpectedPopup能识别技能专用详情，却无条件RequireRecovery，导致combat.unowned_skill_detail。先手动关闭才能启动的处理不算自动修复。
- run2完成原剩余99轮的第1轮；run3并非一直正常运行，最终FLOW_TOTAL_DEADLINE，剩余批次停止在1/99。原先跨版本完成1轮，两批合计2轮，不是连续100轮证据。
- run3事件1618确认chest-disarm输入结果，1622返回子流程；1624父流程选择QuickDisarm1，1625再次进入SharedStep3。页面随后进入真实奖励，子入口仅有disarm动作候选，持续successor_not_confirmed而没有奖励出口，60秒后触发恢复。diagnostics/1.png即真实“獲得了破碎的徽章×9”奖励页，不是战斗被误认宝箱。
- 后续恢复到指令排队的战斗页（diagnostics/9.png、10.png）；倍速HUD改变后combat_active只靠masked ACTIVE与倍速/详情佐证，漏掉独立的战斗指令菜单，ReconnectBoot最后总期限耗尽。

## 修复范围

- 只在技能专用详情与battle同时成立时，UnownedDetail改为Back关闭，等待菜单/结束再重新Prepare，不伪造施放或消费技能，不触发Auto，未结输入门禁不变。
- chest-disarm入口增加只读advanced出口：奖励、战斗、再起或已离开宝箱页的迷宫交回外层处理；不重复拆锁，不将子调用返回冒充宝箱/目标击杀完成。未知页保留原恢复规则。
- 保留masked ACTIVE的双证据要求，新增既有繁中逃跑指令文字作为独立战斗HUD；同时加入资源闭包，不降低阈值、不回退英文。
- 正式chest-disarm仅加入entry/advanced节点和3条选路边，原动作及用户参数不改；CAS前bb9a692c228e0dc25e69590119adac02d44df0fdbfc9b4ec9b1ac530f1183ab2，回读21e35f73a2d997be8ccc4466b1e0c85272a493175806673e8ded9c50bb5e54a5。备份在next/.local/architecture-audit-20261010/skill-evidence/chest-disarm.before-reentry.json。

## 验证边界

本轮只针对遗留详情关闭、拆锁到奖励的交接与上述两张真实截图，未新增全量测试、系统内存跟踪或制造异常。

- reentry-real-frames.log：实际失败轮的奖励图识别reward Hit / battle NoHit，指令排队战斗图reward NoHit / battle Hit；Service使用真实只读冻结目录，快照根为独立临时目录，没有放宽租约/哈希。
- reentry-closure-3.log：生产图/执行器关闭遗留详情；拆锁后转奖励、第二次子调用零输入返回；初始已是奖励页零输入返回。无Auto、无重复拆锁。之前两个夹具失败日志保留：默认空有效技能目录触发孤儿节点、仅覆盖复合scene未覆盖真实目标条件；修正为有一项技能的隔离配置及真实chestOpening叶子观察，不改生产门禁来让测试通过。
- 源码/资源/候选构建及部署日志为reentry-target-build-4.log、reentry-product-build.log、reentry-service-deploy.log，均exit0。product_inputs_sha256=55e53f6541e03991dbb8bbe1ee3ceaae66fbbe855c1a93c3e5999b04e272cc4d，1289项，source_commit仍fd6b617，含未提交修复。
- 保存流程后再通过CAS调整仅新entry/advanced节点的布局，避免与原用户节点重叠，原动作/参数/原节点布局不变；最终revision=8ce4e0dbc5eac1edcaf782cd87fb852dafe6eb9815c1d952ffdbb30412672b11。
- 服务0C56E9B2-7889-4CC5-A3EA-A209EAEDB5E4、pid53348，原17654/data-root。恢复请求ab54344f-78a7-45b2-bfc7-723c8a554f24，GiantBounty/zh-Hant/98轮，run1实际Running、repeat.active=true、0/98。原两批合计2轮，继续余下98轮，不宣称连续100轮通过。
- 新run1已从当前战斗进入正式技能执行，combat-open-detail输入收到confirmed回执并返回父流程；不是accepted即通过。尚未将新版完整一轮、自然奖励交接或遗留详情的实机自动关闭记为通过，定向证据与实机运行范围区分保存。
- profile SHA256仍3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C；未改战斗配置、未关闭游戏/模拟器/VPN，没有宝石消费。新运行日志在next/.local/c11-flow-product/data/runs/0C56E9B2-7889-4CC5-A3EA-A209EAEDB5E4/1。
