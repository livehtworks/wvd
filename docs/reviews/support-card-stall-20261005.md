# 第15轮友方卡片识别停滞

## 停止原因

- candidate115批次`B05ED249-CFED-493C-B05A-7965E978E9E8`完成14轮，第15轮Failed/NATIVE_RECOVERY_UNCONFIRMED，992.371秒。累计68/100，剩余32轮。
- seq1709–1715已点击爱丽丝右下“反叛之祈禱”并确认详情打开；seq1725无等级可选，一级动作按配置返回友方/确定/敌方分支。页面没有等级按钮是正常情况。
- `diagnostics/1.png`中明细0.95057、关闭0.98107均Hit，但support_selection为ambiguous。左上增益图标粘连卡片，候选[51,1096,262,183]，其它上排约[324,1122,261,157]及[599,1119,260,160]。算法只能从左列向右建网格，无法由完好中/右列补回有真实边框的左上卡片。
- 等级调用返回后缺少角色切换、战斗结束和详情关闭出口。无法确认目标后连续三次60秒重启，源节点仍是SelectLevel。
- 最后观察恢复180秒未成功，外层确认游戏在后台、VPN已连接；StartApplication只调用一次并等待前台120秒，最终未确认。新鲜图是安卓桌面，模拟器响应。补发同一个am start将存活任务带回前台，随后焦点确认游戏。

## 修复

- 用完好的中列或右列也可锚定两行三列网格，缺轮廓格必须四条真实边存在；同一物理网格按逐格IoU合并，真正多布局或卡片缺失仍拒绝输入。新增有界候选矩形证据，不读角色数值或降低按钮阈值。
- 等级返回后承接ActorChanged/Ended/ResourceError，详情关闭后仅同一角色重新打开；不消费技能、不宣布施放成功、不启用持续Auto。
- 未取得前台时每3秒在新鲜绑定状态下再次拉起，沿原期限，确认后停止补发；保留VPN/设备前置条件。

## 证据

- 旧卡片检测在故障原帧复现BUFF_JOINED_CARD_MISSING；正式probe见`next/.local/stall116-support_selection-probe.json`。试探未登记的combat_detail模式返回WVD_RECOGNIZER_UNKNOWN，未用于判断。
- 新鲜桌面图`next/.local/stall116-current.png`；第15轮原结果与4张异常图保留。未制造实机断线或死亡。
- 故障原帧已通过6卡检测、六个点击中心及遮盖一张卡片拒绝输入；既有友方/敌方、平移布局和正式战斗图编译通过。初次完整检查因夹具未选择自己创建的test组落到已记录的空方案孤立节点；补齐夹具DEFAULT_OVERALL_STRATEGY=test后原断言通过，不改正式profile。前台未确认补发及既有设备边界检查通过，正式保存巨人配置的繁中依赖完整，2230节点编译通过。
- 检查日志：`next/.local/stall116-card-old.log`（修前真实失败）、`stall116-card-check.log`、`stall116-support-check.log`、`stall116-devices-check.log`、`stall116-giant-check.log`。

## 部署

- candidate116已部署17654/PID48136，服务实例`97395231-8746-49A1-8BBE-0D569C33243A`，保留原data和profile。日志`next/.local/stall116-deploy.log`。
- 请求`b9ce5912-e890-4c47-a625-d4de4e64392c`目标32次，run命名空间`6F50586C-82C6-4975-A12D-B131B1BCA676`。实跑已回战斗，seq398点击Skill1友方左上[182,1200]，seq402后置confirmed；Skill2故障同款尚未自然验到。用户要求暂停后run1已UserStopped/quiescent、result_saved，循环inactive、完成0轮；累计68/100保留，剩余32轮，不自动恢复。
- 停止状态`next/.local/stall116-user-stop-state.json`及新鲜截图`stall116-user-stop.png`保留，正式run目录保存完整停止终态与执行事件。
- 第15轮失败前WVD约816MiB，整批释放后102.7MiB，故障前系统压力样本93.8–94.7%；内存归因仍未闭合，不能将该样本认定为OpenCV泄漏。

## 重新启动100轮

- 用户完成日常后要求重新跑100轮。新鲜截图确认要塞城市，启动请求`f2e9d5a7-376b-40c9-814b-03141aa2646d`，正式run`6F50586C-82C6-4975-A12D-B131B1BCA676/2`已Running/repeat active/目标100，不把历史68轮加入本批计数。
- candidate116原进程PID48136启动前私有内存51.6MiB、工作集75.44MiB、317句柄。沿用debug及memory/performance/recognition开关，正式日志目录仍为原data/runs，未新增重复测试或修改用户配置；内存归因继续开放。
