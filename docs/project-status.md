# 项目当前事实

## 当前产品

- 当前产品在 `next/`，C++20/CMake + Vue + OpenCV + ONNX Runtime；Maa和旧Python链不参与原生构建/运行，历史源码、config、mod、dist不删除。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有循环；没有新增并行执行器、兼容别名或自动回退。
- 配置/流程/回执数据权威见 [数据职责](data-authority.md)，执行注意项见 [操作约束](execution-notes.md)，核心语义见 [核心合同](../next/docs/core-contracts.md)。

## 当前源码候选与现场范围

- 整链整改后完成独立实机一轮并正式部署。当前GiantBounty/zh-Hant请求giant-remaining-action-first-20261011-e75b589e8353449e94fabf99d94ef20b，续剩余71轮；启动核对时实际Running/run1/0新成功，从公会产生新输入和截图。此前成功29=原9+前批run1–19的19+独立实机1；run20修复暂停不计成功，旧22轮等排除。定时检查仍PAUSED，正式profile与workflows逐文件未变。唯一当前账目为next/.local/giant-100-phone-reset-20261011/launch-ledger.json，不能再次累加其中completed_segments。
- 正式实例563E32FD-B980-4FDC-A473-48244D2F0761/PID62136/start_filetime134362070461905839；EXE7438bc031679cb0e354aa59780c8a88e1122e9a552295c591d8caa80713f59a6，输入386a8776a700d3c7a591f158fc3030dc4aee4de6ebe83d033de273955495f12d/1299。原端口17654、data-root保持；后续须核真实身份。100ms强制重做已撤除，复用仍有效的同帧同请求结果；迷宫、恢复、返城、旅店退出按当前页面优先。af8四模块52入口与真实截图/保存副本组合通过；不是游戏52轮。af7栈故障定位到测试入口大局部栈并拆分，未扩大栈或冒充历史内存归因。
- 独立实机CD353AB4-5595-44DD-8858-27207BD5FEE2/run1已Completed并保存完整回执，773.370秒、无重启，悬赏及住宿完成。进本步骤至标记接受为0.870秒，旧五轮同口径5.818–7.440秒；本轮未出现宝箱，不能宣称宝箱后同条件实机耗时已通过。一次恢复打开等待11.77秒后转入新战斗，仍列性能复核项，不凭耗时推断原帧或ROI问题。详见[整链整改](reviews/action-first-chain-20261011.md)、next/.local/af-live-261011/acceptance.json。内存仍RESOURCE_UNRESOLVED。
- 2026-10-12按用户要求整理提交与推送：范围为V2工作包、后续已验收的流程/识别修复及对应报告；用户独立.vscode配置不纳入。正式产物由提交前候选构建，source_commit仍记录d03a765，完整代码身份由上述product_inputs_sha256绑定；Git提交不自动重建或重启正在运行的服务。提交及远端确认以Git历史为准。

以下是前次普攻部署的历史索引，不作为当前实例/批次指针。

- 最新普攻修复已部署：旧柚奈普攻保底套用了仅详情页成立的support_selection(absent)，真实原帧为unknown/NoHit，61.308秒触发脚本主动重启；不是自然闪退证明。现已撤出独立普攻点位，复用combat-select-target/skill_target，attack_mode区分无详情普攻与正常单体技能；同一原帧选出(369,281)并通过真实门禁送达隔离后端。at3的49个入口及正式profile四种图组合通过，公共选敌保存定义原revision CAS迁移、用户参数/profile保持。详见[普攻选敌闭环](reviews/attack-target-handoff-20261011.md)。
- 当前续跑giant-100-shared-target-20261011-eda98023979441e996642952a3d61fdc/GiantBounty/zh-Hant/repeat_count100已实际Running/run1，0成功，从Boot_Attention推进到Boot_Title并持续取得新帧，VPN CONNECTED。正式实例0364D50B-F3AB-4790-92B3-76ED27162C39/PID12560/start_filetime134361716761599893，EXE2d707c605e68383bdc11f86b38f0fd74648cda095b275fb52e1e2da843428bfc，输入ce428d74bbae1684626965d3be44c8f7db62c85a031190a3af087d69e6d001f8/1299项；原端口/data-root保持。新100目标沿原launch-ledger，所有0成功修复尝试及旧22轮排除；不宣称已完成100轮或实机无SP攻击成功，待自然日志证明，不自动commit/push。

以下阶段修复记录属于前次部署，当前实例/请求以上述最新指针及launch-ledger为准。

- 当前阶段优先与无SP手动攻击保底已正式构建/部署；49个app/recognition/runtime合同入口、两张真实开箱/奖励Service验证及保存库副本八种整链受控场景通过。两份正式宝箱步骤原revision CAS迁移并回读，profile/坐标/延时/布局及其它参数不变。救人、多人死亡人工停止、Unknown输入和宝石限制保留；没有持续Auto或新增卡死探测。详情与证据边界见[本轮报告](reviews/current-phase-priority-20261011.md)。
- 最新现场：请求`giant-100-phase-priority-20261011-1368d3e0e97c4a858350ffd3827b1b86`/GiantBounty/zh-Hant/repeat_count100已实际Running/run1，成功0；新帧已从奖励继续、返城、公会、跳轮推进到迷宫标记导航。正式实例E93F3276-872F-446C-8774-2D5DB7CDD157/PID16764/start_filetime134361673322405857，EXE080d3d6b5917276d71ef1ece75772813799256c1b4422e1fcb15e6d0414e8abe，输入1a5304e017c3594daa391b3eb45eb4fa79b3a3c85f00d336e3116b9f1dabd010/1299项。端口17654、data-root保持，VPN CONNECTED。账目`next/.local/giant-100-phone-reset-20261011/launch-ledger.json`与小时巡检指针已更新；旧22轮和修复前run3的0成功尝试排除。不宣称100轮、自然无SP普攻或宝箱后同条件恢复延迟已通过；内存仍RESOURCE_UNRESOLVED，不自动commit/push。

## 较早承接记录

下方是历史现场与验收索引，不覆盖上方当前实例、批次或授权。游戏彻底无交互官方bug仍由用户处理，保留现场、不反复重启；其它恢复职责保留。
- 2026-10-11现场继续：用户说明无交互应按卡死处理，并要求先推进看现场。哈肯/宝箱手动探测无反馈，关闭游戏标签后仍相同；真实force-stop已验证游戏PID退出，随后MuMu实例2宿主退出（原因未归因），经原设备连接入口恢复可见实例2。当前正式实例1469931A/PID34172未换版本，单轮请求`giant-recovery-live-20261011-d9f481a67bd74168931a5d0878a86bc7`/run2已实际Running，从黑屏启动返回迷宫并执行返哈肯；Clash Meta VPN CONNECTED/tun0/VALIDATED。仅单轮现场反馈，不重复启动连续批次；累计仍22，完成回执才能增加。新的无响应宝箱探测源码尚未部署，不能当作已生效。此前“等待确认哈肯交互方式”的阻断已由用户现场说明取代，旧停止回执保留。
- 2026-10-11 Windows承接：ZIP SHA256 `6c4d3d97b1863864952a0c4c41aca231137910e2a70d3586bc965f9682aaf88c`、2204份索引文件及64个基线Git blob已核对；累计补丁已应用，`.vscode/`保留。另补7000G顶层拒绝下载范围、独立救人职责声明与真实Call/append owner校验，公共四流程声明同步。六模块contracts-final-7及仅测试夹具变更后的app/recognition contracts-final-8均complete=true；未变模块源/二进制复用证明及合并回执在next/.local/followup-v2-20261011/acceptance.json，当前输入`134747d460b9bc61c68a19422b4f6472acf92cdab3a4ac5e25ed7725d388487e`/1299项。它不是游戏通过或所有RA验收关闭。
- 591项资源载荷已全量核对；五份正式保存流程经原revision CAS精确迁移并回读，用户参数/布局及profile哈希未变。实际保存库20份公共定义接入受控整链，八场景Completed/pending=0；其余五份用户流程未宣称完整执行。正式构建、冻结包、空闲部署及EXE/PDB只读Preflight已完成；没有启动模拟器、游戏、循环或新采集。
- 新100轮累计成功22轮（原4+首次有效续跑18），失败run5/run19均不计数。正常结算后转宝箱、返城Entry遭遇交接修复保留；最新仅修返哈肯静止交接反复重进导航，保留原60秒无进展等待及已有恢复出口。最终47个app/recognition/runtime入口及补充harken-no-route通过，完整构建/冻结包输入d854762d0e61347432f3da388005a177b1ce98273f3b31dcda93cdba4234f557/1299项。正式实例1469931A-0028-4A3D-816E-7134FDDE2AFE/PID34172，EXE602d10c33b82c58038be492beb8e5b90527f25c592399b5971d4a2f1a239c66d；78轮请求giant-78-harken-stall-20261011-1a079d2535ba468db18d878fac291910曾实际Running，但两次恢复仍停哈肯实体前，代理已停止该0成功尝试，服务空闲、游戏/模拟器保留。原Sent标题输入账目保存在session，不伪确认；当前hold_reason为哈肯交互入口未闭合，待合法动作或新现场证据确认后才续剩余78。中间0成功尝试与旧人工停止的2轮排除。[最新现场与修复](reviews/harken-stationary-loop-20261011.md)、[此前转场修复](reviews/target-chest-settlement-20261011.md)及resume-ledger保留原回执与261字符存储路径独立缺口。源码仍未提交，不宣称100轮、宝箱同口径延迟或内存归因通过。
- 用户暂停原批次后授权修复宝箱→恢复延迟、部署并重新开启100轮。正常迷宫/角色/恢复页面排除“再起”OCR，死亡分支先保护；未知页面仍走原OCR。Requested观察并入Begin确认，保留视觉准入和healing_required业务条件，不改ROI、阈值、等待或重试上限。三个模块46个合同入口、两张真实恢复原帧及两张救人/技能详情原帧组合通过；不是游戏复活成功或长期稳定通过。
- 原新100轮批次`giant-100-heal-fix-20261011-0700c9f9f1944e478320b65f7dbdf0d0`及当前剩余78轮均为GiantBounty/zh-Hant。profile revision`d26988cd440cd5f55fbf2ca34e0e4b7b1aaf54dc16d1717d154e0a065b865cab`及SHA`3e873d08fb9135f558c3e3348b3bfffff3b73555c63a9176cc75cff274ccb87c`未改，保存流程未改。沿用原日志/最近240帧策略，不新增WPR/ETL。小时巡检当前累计基数22与新78轮以resume-ledger为准；游戏/救人/Unknown/宝石限制沿用原合同。启动记录`next/.local/chest-heal-latency-20261011/batch-100-launch.md`。
- 新候选实机run1的返城途中战斗结束→打开恢复角色面板3.388秒（事件2771→2793），入口再起OCR调用0，恢复尝试后已继续返城。本次没有掉宝箱，不能冒充旧宝箱后25.1秒的同口径对照；后续自然循环继续采样，100轮尚未完成。
- 补给按[核心合同](../next/docs/core-contracts.md)的尽力尝试语义处理；完整scene成立与独立target可输入分别判断，不能恢复旧的双eligible要求。内存仍为`RESOURCE_UNRESOLVED`，定向分配减少和对象释放证明不等于历史45MiB已归因。

## 先前整改记录

- 用户指定的 `wvd-full-architecture-audit-4c466e1.zip`源码整改/限定离线交付已整理：基线4c466e14fa514fa177380e902afe373c8f20c3b2，54份文件哈希/大小一致。16包不是全部验收关闭，43项未批量closed。
- 架构审计轮只做源码、独立离线/Windows定向验证及文档。随后用户另行授权部署及巨人100轮：已部署并启动正式批次，正式profile/用户流程与`.vscode/`未改；没有新增系统级内存跟踪采集。
- [逐包整改报告](reviews/architecture-audit-remediation-20261010.md)、[43项索引](reviews/architecture-audit-20261010/ISSUE_INDEX.md)及16份JSON回执保留历史验收维度。上一轮整改已提交/推送ee024eb；原回执null是当时状态，不重写历史。
- [后续修复](reviews/architecture-audit-followup-20261010.md)：流程读缓冲OOM句柄泄漏已旧失败/新通过，未跟踪源码漏验与依赖模块映射已修复，跨实例同编号PNG/延迟旧读在实际RunStore通过。六模块后续回执complete=true，产品输入hash60b68ff66c1b5f7733280e6f81cefec56c8f6b9d59dbd148b03906fc64c047e7，随后续源码一并提交；未部署或启动游戏。
- 最终final-contracts-14六模块入口complete=true：39个原生命令、11项工具/构建，另HEAD检查通过；源/10个EXE/4个分析器产物身份复核，产品输入hash为56a9ac97f30875dfd18931f573d2fe57d0616f60f29924111796a1e3a36d242e。这里的50项不是游戏轮数，也不覆盖全部工作包验收断言。
- WP03/07/08/09/11/13/14/15有逐SDK故障、真实ETL/复杂图成本、并发图片端到端、批次边界、全部最小builtin/旧反例等必需覆盖缺口，具体由原回执限定。随后用户纠正补给验收：回复是尽力尝试，不因疗效未确认停止行进；原NoOp断言已改为继续且不虚报疗效。自然崩溃计划仍UNARMED。
- 当前测试入口是 `next/tools/run_contracts.py`，必须指定模块/改动基线和全新隔离输出。个人fork的原生PR工作流已添加，远端CI尚未执行。

## 正式现场基线

- 最近一次部署现场为T0撤回：fedfbf5新增hp_overlay/CloseHpOverlay误把救人页关闭，批次2fa96d84-7998-4c3b-8e7e-6bc234fd00ed/run1已人工停止，0/93。源码完整撤除该分支，原party_death救人代码保持；正式入口当时回滚到该错误分支加入前的完整候选，实例23FC3DF9-FE96-460E-8A5D-049909751C81/pid58992、Idle。2026-10-11承接时17654无服务响应，历史实例不作为当前运行状态。不得自动恢复循环；旧7轮及部分失败轮不计新版完成数，不宣称长期稳定。

- 本批性能排查：前六轮耗时691.060/613.737/1142.091/598.061/509.402/693.269秒，实际模板比对27508–65594次/轮；取图转换合计13.357–28.533秒/轮。战斗Entry平均475–500ms、打开详情等待均值1006–1165ms，尚无持续递增证据。修复采用combat_phase，先当前战斗/详情、再实际转场出口；正式公共打开/结果节点也切换，通用all/any、输入回执及跨帧边界不变。真实菜单/详情/奖励/黑屏的新旧结果一致，菜单ended比对7→1、详情finished比对14→2；缺隐式OCR模型发布拒绝、无效阶段/通用all叶子错误仍Error。证据在next/.local/architecture-audit-20261010/phase-current-battle-frames.log，交接/open/reentry定向验证通过；尚未将这几张图的成本下降外推成实机整轮提速。

- 错误产物fedfbf5/e5094bc8/d2ccfb74已移出正式入口，保留在dist/wvd-next-native.t0-withdrawn-fedfbf5，禁止使用。当前人工回滚候选source_commit=8d3547c（含当时阶段性能修复），输入81fd113333cd13ad02c62e664fec527d4d2733d3682937eaaa3829cb76521bd3，EXE1b58ef43f65b851ed0e8365cdcf070566d665466f7c5e38219af77d39d45e822；不是以当前撤回提交重写旧构建身份。三个正式公共战斗步骤的阶段识别更新保留，profile哈希3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C未变，战斗配置/VPN/日志保持。
- [场景交接修复](reviews/scene-handoff-20261010.md)：上一批2/98后run3目标战斗切到宝箱但外层只接受迷宫，下一战斗使已结束目标失去身份，导致25次重导航并超时。已修目标宝箱终态、统一宝箱阶段条件、宝箱入口转战斗与公共步骤入口交接、战斗/宝箱计时和身份结清；五份保存流程已备份/CAS同步。随后96轮run1已实际确认target_battle_ended_at_chest、task_step1，却在开箱选人后以WVD_CONDITION_DEPTH停止，0/96。已分离条件结构/内部递归、拆出小型布尔求值函数、合并编译时重复包装；真实失败图的完整后置/重试请求、边界/缺图与相关回归通过。新发布4038份请求静态核对最深八层、超限零；这不是4038个测试或游戏轮数。已部署续跑，完成第一轮并自动接续run2；profile/保存流程本轮未改。新一轮没有再次自然进入开箱选人，不能把离线复现冒充该页实机通过；长期运行仍待观察。
- 旧巨人100轮批次ae558656-d04f-4545-a6be-46fa3e4a5f0d在第1轮回复段以supply.healing_outcome_unconfirmed停止，0/100完成。修复后批次96d9fb78-f81c-4ded-ac6a-58034042c72a完成1/100，run2因排查灰色技能重复点击已人工停止、quiescent=true；run1实际完成断点收尾、新跳轮/刷新、巨人战斗、回复、返城提交和金币住宿。不是连续100轮通过，最新以API/日志为准。
- [技能不可用修复](reviews/skill-unavailable-20261010.md)：全技能灰掉被bright_peer条件漏判，公共入口11次点击/约21秒后才防御。新识别改指令栏亮文字参照；同页打不开详情最多补点3次并观察5秒，仍转手动防御、不消费未施放技能。正式保存定义已CAS同步并部署；已实机出现面具灰色技能，两帧识别后不点技能入口直接防御、推进下一角色，检查时11个输入中Auto点击0次。
- [重入与页面交接](reviews/runtime-reentry-20261010.md)：遗留技能详情改自动取消重看菜单；拆锁入口增加已到奖励/战斗/迷宫的只读出口；指令排队战斗补既有逃跑菜单HUD佐证并同步资源闭包。真实失败图、生产图/执行器定向验证通过，保存流程CAS同步并部署。新版已实际进入战斗技能执行；尚未宣称新版整轮、自然奖励交接、遗留详情实机自动关闭或长期稳定性通过。
- 部署修复了候选身份字段陈旧、冻结目录用斜杠拼接导致语义/公共流程/OCR目录漏读、扩展路径祖先误判及根路径表示不一致；没有降低资源哈希/链接/租约安全检查。冻结目录加载和发布复制针对性验证已通过。首次准备失败及首个失败run日志均保留，详见[部署记录](reviews/deployment-100-20261010.md)。
- 最后批次8/41完成、第9轮停止；跨版本/跨恢复累计67/100，不是连续100轮通过。停止前Heal段46.352秒、1199次实际模板比对，取图转换0.334秒、识别调度34.906秒。
- 实机已验证的历史蝎女/巨人流程仍有证据；59项公开目录没有被禁用。Fordraig/Cave工厂是未公开休眠实现，不算新增可运行任务。
- 历史100轮批次的操作许可为部署并启动巨人，实例2、繁中、已有VPN配置，金币可按任务使用；禁止绿色/紫色宝石购买、抽卡、卖装备。当时保持战后/开箱恢复配置。该批次后已发生T0并停止，不能沿用这条历史授权恢复当前循环；疗效未确认不单独阻断的现行语义见核心合同。

## 开放边界

- T0后原只读审查确认救人职责被补给过滤、重连没有本地出口；这些漏接已修且进入当前空闲部署，根冷启动与重连职责仍分开。六个工厂/四份公共流程的独立职责、真实调用及五份保存版本迁移已核对，不推导全部业务调用边及现场接受已完成。重复处理的后续复审基线为至少下载5、Pause2、普通剧情6、旅店退出2、补给提示2、技能取消3处；共享配方、各调用者终点与实际消费情况以继续修复回执为准，不能把重复定义数当成已发生的事故数。原始事故事实保留在[同页多处理器审查](reviews/restart-loop-20261010.md)。

- `RESOURCE_UNRESOLVED`：新聚合器/端点/屏障的离线验证不等于真实负载分配栈归因。后续短采集须单独授权，不再靠增加轮数或PrivateUsage标量关闭问题。
- libRenderer.dll/0xc0000409自然崩溃缺dump/故障栈；历史nvoglv64.dll签名、程序主动重启分别保留，不能混为原因。取证计划保持未启用，厂商SDK契约未证实前不改尺寸查询/缓冲复用。
- 用户已明确回复是尽力补给，沿旧代码点回复、关闭面板、继续；不以法力/道具充足或全员满HP为推进前提。当前部署已移除回复疗效未确认的必停出口；只有实际提交过操作才能消费尝试，送达未知及疗效未验证单独保存，不虚报治疗成功。没有新增HP读取或回复提示OCR。定向验证通过；实机run1事件3969记录回复输入，事件4000记录healing_attempt_finished/effect_status=not_verified，随后完成报告、住宿并续run2。
- 历史面具技能面板图缺失，头像命中不能倒推出技能可点；多人死亡页按用户要求人工停止；部分普通迷宫随机事件繁中素材仍待自然采集，不放行实际依赖缺失的流程。

## 历史索引

- [整改前完整状态归档](reviews/project-status-before-architecture-audit-20261010.md)
- [暂停问题交接](reviews/runtime-issues-handoff-20261009.md)
- [MuMu自然崩溃核查](reviews/mumu-renderer-crash-20261009.md)
- [架构审计本轮交付](reviews/architecture-audit-remediation-20261010.md)
- [架构审计后续修复](reviews/architecture-audit-followup-20261010.md)
