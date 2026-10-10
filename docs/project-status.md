# 项目当前事实

## 当前产品

- 当前产品在 `next/`，C++20/CMake + Vue + OpenCV + ONNX Runtime；Maa和旧Python链不参与原生构建/运行，历史源码、config、mod、dist不删除。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有循环；没有新增并行执行器、兼容别名或自动回退。
- 配置/流程/回执数据权威见 [数据职责](data-authority.md)，执行注意项见 [操作约束](execution-notes.md)，核心语义见 [核心合同](../next/docs/core-contracts.md)。

## 当前整改

- 用户指定的 `wvd-full-architecture-audit-4c466e1.zip`源码整改/限定离线交付已整理：基线4c466e14fa514fa177380e902afe373c8f20c3b2，54份文件哈希/大小一致。16包不是全部验收关闭，43项未批量closed。
- 架构审计轮只做源码、独立离线/Windows定向验证及文档。随后用户另行授权部署及巨人100轮：已部署并启动正式批次，正式profile/用户流程与`.vscode/`未改；没有新增系统级内存跟踪采集。
- [逐包整改报告](reviews/architecture-audit-remediation-20261010.md)、[43项索引](reviews/architecture-audit-20261010/ISSUE_INDEX.md)及16份JSON回执保留历史验收维度。上一轮整改已提交/推送ee024eb；原回执null是当时状态，不重写历史。
- [后续修复](reviews/architecture-audit-followup-20261010.md)：流程读缓冲OOM句柄泄漏已旧失败/新通过，未跟踪源码漏验与依赖模块映射已修复，跨实例同编号PNG/延迟旧读在实际RunStore通过。六模块后续回执complete=true，产品输入hash60b68ff66c1b5f7733280e6f81cefec56c8f6b9d59dbd148b03906fc64c047e7，随后续源码一并提交；未部署或启动游戏。
- 最终final-contracts-14六模块入口complete=true：39个原生命令、11项工具/构建，另HEAD检查通过；源/10个EXE/4个分析器产物身份复核，产品输入hash为56a9ac97f30875dfd18931f573d2fe57d0616f60f29924111796a1e3a36d242e。这里的50项不是游戏轮数，也不覆盖全部工作包验收断言。
- WP03/07/08/09/11/13/14/15有逐SDK故障、真实ETL/复杂图成本、并发图片端到端、批次边界、全部最小builtin/旧反例等必需覆盖缺口，具体由原回执限定。随后用户纠正补给验收：回复是尽力尝试，不因疗效未确认停止行进；原NoOp断言已改为继续且不虚报疗效。自然崩溃计划仍UNARMED。
- 当前测试入口是 `next/tools/run_contracts.py`，必须指定模块/改动基线和全新隔离输出。个人fork的原生PR工作流已添加，远端CI尚未执行。

## 正式现场基线

- 最新现场：上述96轮批次已完成8/96，run9因住宿后正常提示箭头被小ROI排除进入6次游戏重启。后续Boot已实际Ready/Terminal，但resume_event先判旧Leave超时、未检查已恢复城市，导致重复重启。已停止run9及循环，日志/模拟器保留。修正版已部署：旅店BACK退出菜单即完成、提示可选、先看恢复新帧，原图复核和相关限定回归通过。新服务Idle、busy=false、quiescent=true，未恢复循环；本次实机整轮/长期稳定未验。证据见[重启循环修复](reviews/restart-loop-20261010.md)。下方run1/run2为之前验证记录，不是当前运行状态。

- 2026-10-10当前部署产物：source_commit=fd6b617c9425bea3379e32d7b5a7812fff3b8d9c，product_inputs_sha256=f068d3cb93428fae31df90f3905afa0781a59e638396150ac658595aab3da33d（含本轮未提交修复），1289个产品输入，EXE=8eae4ed0b3c351f6cc4655958cec21edd4c95be5fde31bfa819661d393b2492e。服务在17654、原data-root；实例3891BA20-7485-49CD-B15A-60D58E90F34D、pid22400，当前Idle无循环。原续跑批次fa827b34-27bc-40cd-8636-41bd65c3d6c7已完成8/96、run9已UserStopped。此前合计完成4轮；目前跨批合计12轮，不是连续100轮证明，最新以API/日志为准。profile文件哈希/revision保持不变。
- [场景交接修复](reviews/scene-handoff-20261010.md)：上一批2/98后run3目标战斗切到宝箱但外层只接受迷宫，下一战斗使已结束目标失去身份，导致25次重导航并超时。已修目标宝箱终态、统一宝箱阶段条件、宝箱入口转战斗与公共步骤入口交接、战斗/宝箱计时和身份结清；五份保存流程已备份/CAS同步。随后96轮run1已实际确认target_battle_ended_at_chest、task_step1，却在开箱选人后以WVD_CONDITION_DEPTH停止，0/96。已分离条件结构/内部递归、拆出小型布尔求值函数、合并编译时重复包装；真实失败图的完整后置/重试请求、边界/缺图与相关回归通过。新发布4038份请求静态核对最深八层、超限零；这不是4038个测试或游戏轮数。已部署续跑，完成第一轮并自动接续run2；profile/保存流程本轮未改。新一轮没有再次自然进入开箱选人，不能把离线复现冒充该页实机通过；长期运行仍待观察。
- 旧巨人100轮批次ae558656-d04f-4545-a6be-46fa3e4a5f0d在第1轮回复段以supply.healing_outcome_unconfirmed停止，0/100完成。修复后批次96d9fb78-f81c-4ded-ac6a-58034042c72a完成1/100，run2因排查灰色技能重复点击已人工停止、quiescent=true；run1实际完成断点收尾、新跳轮/刷新、巨人战斗、回复、返城提交和金币住宿。不是连续100轮通过，最新以API/日志为准。
- [技能不可用修复](reviews/skill-unavailable-20261010.md)：全技能灰掉被bright_peer条件漏判，公共入口11次点击/约21秒后才防御。新识别改指令栏亮文字参照；同页打不开详情最多补点3次并观察5秒，仍转手动防御、不消费未施放技能。正式保存定义已CAS同步并部署；已实机出现面具灰色技能，两帧识别后不点技能入口直接防御、推进下一角色，检查时11个输入中Auto点击0次。
- [重入与页面交接](reviews/runtime-reentry-20261010.md)：遗留技能详情改自动取消重看菜单；拆锁入口增加已到奖励/战斗/迷宫的只读出口；指令排队战斗补既有逃跑菜单HUD佐证并同步资源闭包。真实失败图、生产图/执行器定向验证通过，保存流程CAS同步并部署。新版已实际进入战斗技能执行；尚未宣称新版整轮、自然奖励交接、遗留详情实机自动关闭或长期稳定性通过。
- 部署修复了候选身份字段陈旧、冻结目录用斜杠拼接导致语义/公共流程/OCR目录漏读、扩展路径祖先误判及根路径表示不一致；没有降低资源哈希/链接/租约安全检查。冻结目录加载和发布复制针对性验证已通过。首次准备失败及首个失败run日志均保留，详见[部署记录](reviews/deployment-100-20261010.md)。
- 最后批次8/41完成、第9轮停止；跨版本/跨恢复累计67/100，不是连续100轮通过。停止前Heal段46.352秒、1199次实际模板比对，取图转换0.334秒、识别调度34.906秒。
- 实机已验证的历史蝎女/巨人流程仍有证据；59项公开目录没有被禁用。Fordraig/Cave工厂是未公开休眠实现，不算新增可运行任务。
- 本次操作许可：部署并重新启动100轮巨人，实例2、繁中、已有VPN配置，金币可按任务使用；禁止绿色/紫色宝石购买、抽卡、卖装备。未关闭配置中的战后/开箱恢复来绕过治疗确认限制。

## 开放边界

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
