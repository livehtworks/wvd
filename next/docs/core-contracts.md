# 当前核心合同

解释入口与真实调用链对应，不另建产品执行器，不依赖历史报告猜许可。

| 合同 | 唯一解释入口 | 不变式 |
| --- | --- | --- |
| 输入事实 | PendingInput/PendingDelivery、FlowExecutor | 底层调用前登记Prepared/Attempted；Sent/DeliveryUnknown先落已有对象，后续分配失败不抹未决输入；确认结果才结清 |
| 页面与业务 | Observation/PageAuthority/input_evidence_matches_frame、NativeInputGate | scene须Hit，独立target须Hit且eligible，两者必须绑定本次完整帧身份；只读scene不取得输入资格，固定点/back/swipe不绕过；BusinessConfirm独立检查condition，页面返回不是效果证明 |
| 有效策略 | effective_strategy_name、配置规范/角色选择 | 空任务配置继承唯一全局，未知名拒绝；先选当前最佳角色，再看剩余动作，不用次高分替代；频次按实际释放回执 |
| 恢复证明 | InstanceExitProof/instance_exit_covers、生命周期deadline | 绑定设备/实例/创建身份/旧新连接/退出时间/事务；捕获≤提交≤退出，旧证明不清后来输入；取消/超时先于ready |
| 资源闭包 | CompiledWorkflow/builtin_probes/ModelContract/manifest | 实际语言、隐式模板/OCR模型均须冻结且哈希一致；缺失为Error并拒绝发布，不当NoHit，不靠完整基础包掩盖遗漏 |

## 运行与读取

- 单人救人观察/成功返回不重载战斗策略：仅撤销旧的已准备输入选择，保留当前遭遇、已消费动作、剩余技能和策略组。只有真正执行并确认再起才按既有resurrected职责重载；死亡页消失不是再起证明。
- 步骤间等待由作者流程的wait节点负责，位置与duration_ms由流程保存数据决定。巨人目标结束后调用可编辑的after-target-battle公共步骤，默认直接继续，不在战斗/任务计划中硬编码3秒。已归属且已结算的目标推进仅核业务账目；下一页面和下一输入仍单独核当前帧，不能用加载过渡页否定已完成战斗。
- 正常副本入口先验证迷宫场景并执行当前路线；导航子流程先尝试当前目标动作，失配才交接。恢复开角色后等待真实面板/遭遇，未打开时仅在原角色、当前迷宫和原输入门禁仍成立时重试，不再固定等待2秒或用未变化迷宫帧当打开成功。

- `checks.inherit`仅是继承白名单，不充当业务职责的验收答案。既有救人职责由`require_inherited_events`/作者`checks.required`独立声明；编译器核实际Call/handler父策略、节点禁用、真实owner及原业务体，append不伪造调用帧。缺少已提供职责时报完整源路径，不自动扩充规则；独立工厂无父提供者记为undefined，不报产品职责通过。活动处理器的同ID防递归保护不取消，校验仅在构图/编译期执行，不增加逐帧扫描。
- 场景所有权与业务终态分离：已确认战斗→宝箱/宝箱→战斗时，退出旧页面处理并按新场景分派，不依赖瞬时迷宫帧或具体奖励名称。宝箱处理段退出不证明奖励到账，战斗页面退出不自行证明整个副本完成。目标战斗另核unit/point/combat身份后推进目标；临时遮挡保留未决输入并重新观察，未知页面不是成功证明。
- 战斗子流程已在短暂迷宫帧正常结算时，随后宝箱结果可使用同一目标遭遇的settled_combat_sequence，不要求pending_combat继续为true。两条正常结算入口记录该序号，复活撤销不产生正常结算记录；unit/point/combat、phase、恢复授权及死亡保护仍须成立。已经结清的战斗不重复累计，宝箱奖励和宝箱完成仍独立核对。
- 返哈肯的暂时静止不结束当前导航，也不证明到达楼层菜单；同次Moving保留既有无进展窗口，仅真实位置进展续期。楼层/祝福/郊外和遭遇仍优先分派，持续无进展沿原StalledExit恢复，不经stopped交接循环重进刷新期限。标记/宝箱搜索的停止与完成合同不变。

- Application → Coordinator → Session → FlowExecutor唯一链；Application拥有批次，旧Python/Maa不参与原生构建/运行。
- current一次ReadView；事件按cursor读取并绑定instance/run/gen，缺区间明确resync。图片URL含instance/run/gen/id并核不可变索引/哈希，不使用“当前图片”别名。
- 停止不等command mutex、准备磁盘I/O或诊断图片写入完成；stop_epoch阻止停后新准备。收尾仍等真实worker退出，不能用停止请求成功代替静止。
- 一个根run拥有OCR模型池，unit/recovery借用；取消终止当前运行，不能清活跃引擎后复用。生命周期配对/归零不证明所有Windows提交增长已归因。
- 迷宫补给沿旧StateDungeon的尽力尝试语义，不以MP/道具充足或全员满HP为推进条件。healing_required表示当前遭遇尚欠一次恢复尝试，不是实时血量判断；WvdHealingEffect记录实际输入，healing_attempt_finished在已提交回复操作、角色面板关闭并回到当前迷宫后消费尝试。日志healing_effect_status=not_verified，送达未知标志另存，不虚报治疗成功，也不以疗效未确认阻断行进；generic healing_completed仍不得用页面返回伪造疗效。未发送不消费尝试；网络等覆盖层仍沿现有事件处理，且不重复回复来追求“满血”。

- 住宿沿旧StateInn：确认房型付款后，在旅店菜单按BACK退出，菜单退出即住宿步骤终点；不以“背包补充完毕”文字、满HP/MP或额外城市帧作为终点。上层仍核实际住宿回执及付款非pending，不能凭城市图伪造住宿成功。剧情/补给提示仅在出现时处理；箭头搜索下半屏、点击真实命中中心，并保留剧情控制、战斗和宝石购买反证。
- 事件恢复接续先识别当前resume_guard，再在NoHit时判断旧业务节点超时；旧节点过期不能否定已经回到有效场景。停止/生命周期截止及未决输入的原后置、身份、送达核对不放宽。菜单输入补试只支持普通Click和BACK键4，不允许HOME/POWER等系统按键借此自动重放。

## 识别与预算

- 布尔条件结构最多八层；这是结构合同，不是游戏重试次数。WvdVision在请求入口校验，内部内置配方调用另受32层递归防护。布尔树由独立小函数求值，不重复携带场景分派的局部栈空间。正常all按声明顺序求值，遇NoHit停止检查不再影响结论的后项；已执行项的Error必须传播，所有项命中才可授权输入。any/not可能将失败的子项组合成Hit，因此它们及其子树仍汇总各项输入资格否决，不用省略后项提升授权。完整可达资源在编译冻结/发布阶段验证，不能因运行时某分支未执行而遗漏。超过16子项保留原结构，不扩展运行合同。

- Service内有界叶子身份登记，同帧/epoch共享；并行重复叶子只有一生产者。组合和业务状态不跨帧缓存，语言/ROI/阈值/TTL不放宽；any/all仍传播所需叶子Error。
- 战斗阶段使用combat_phase：先识别当前combat_active；有效战斗直接排除迷宫/宝箱/再起出口，详情仍开直接排除施放完成。仅当前阶段无法成立或已离开战斗时才展开所声明的出口，未知/黑屏不当完成。公共打开详情与确认结果、角色/遭遇/Auto子流程使用同一阶段配方；没有新执行器或跨帧场景缓存。编译冻结全部可达模板，发布核对隐式OCR模型，缺失不因运行时跳过某分支而放行；通用all/any的Error传播合同不变。
- 输入候选按业务标量、当前scene、独立target的顺序准入；scene为NoHit时不扫描该候选按钮，scene/target的Error仍传播，实际送达门禁仍核同帧完整身份。无条件Input也须成立才可选中，不能阻塞后面的有效分支。已命中正常业务候选不进入Exception/Special穷举，显式覆盖层保护保留。
- 识别耗时本身不使截图失效，不设置100ms的强制重做门槛。当前源码按原设备/连接/输入身份检查结果是否仍可使用；相同有效帧和相同请求的已命中结果可从选路交给执行，不重复识别。等待、实际输入及重复进入同一选路点仍建立新观察；截图身份失效时连同已选结果一起撤销，包括不直接输入的子流程调用。此为项目实现合同，不是行业规定；候选验收边界见docs/reviews/observation-age-rule-20261011.md。
- 宝箱使用chest_phase优先当前阶段；奖励页不再授权开箱/选人/拆锁，识别不到具体道具不影响继续。阶段失配时才展开声明的迷宫、战斗、再起等交接，死亡反证保留；全部可达模板/实际语言OCR闭包在发布时校验。普通开箱页排除再起OCR，不改变通用布尔求值合同。
- 技能因资源不足或灰色不可用时，保底为同一行动角色的手动攻击指令及敌方目标选择，不是防御或持续Auto。仅实际行动推进确认attack_fallback_confirmed；未施放技能条目不消费。用户明确配置的防御动作仍保留。
- 普攻与单体技能共用combat-select-target/skill_target的NEXT、标记和区域尝试，不另设普攻点位算法。attack_mode默认false保留详情页语义；true要求同一准备角色、正常战斗指令页且无详情/OK，输入坐标仍由共享识别器独立授权。support_selection(absent)只用于详情场景，其unknown不能当absent，也不能用它排除普通攻击页。普攻目标观察100ms，技能默认延时及用户值保持；已选攻击不是实际行动完成证明。
- 救人页只能由既有party_death分支处理，不能凭血条/关闭按钮把它当普通覆盖页取消。2026-10-10新增hp_overlay/CloseHpOverlay导致取消救人，已按用户要求彻底撤除识别模式、图节点、按钮配方及验证白名单；不得以“页面已消失”宣称救人成功。原有中心点击救人及再起/多人死亡边界保持；离线原图通过不等于实机救人成功，实际多人死亡/Unknown保护停止仍须保留现场等待用户许可。循环启动授权及当前状态以最新用户指令与project-status为准，不把T0当时的临时停机状态永久写成启动合同。
- 输入送达未知时保留原pending和期限，禁止补点、处理器输入及推测性重启；当前有效事件作用域内，只有无handler/replan/resume_guard的直接ExternalBlocked规则可以凭本次完整帧的Hit停止。NoHit不能确认停点，Error/旧帧不能冒充救人证据；该只读检查不确认原业务效果。多人死亡沿现有人工停点，单人救人仍走原分支和中心输入。
- 有效事件清单按program/step/active scope缓存；语义进度与elapsed心跳分开，不每tick构造整份进度。
- 并发workspace与OS压力准入分开；系统提交、进程虚拟余量、可查询Job进程提交上限独立。未知Job当前总提交不用峰值代替。模板/解码/预处理/OCR共用票据，压力是Error，不重放输入或清pending。
- 采集屏障默认关闭；worker_joined/heap-maintenance同阶段端点绑定request/instance/run/gen/config/sequence/创建身份。到期/stop取消，joined不改名为batch_payloads_released；容量/期限/覆盖不足明确拒绝分析。
- 输入审计与性能采样分级限额；普通采样丢弃报告sampling_complete=false，不假造完整、不因它终止业务续轮。关键输入遗漏仍阻断；关键失败图有独立额度与永久run索引。

## 验证与公开范围

`tools/run_contracts.py`核对实际CMake目标，记录源hash、EXE hash、隔离根、限定通过项；未指定模块/基线或隔离输出拒绝。12组既有Flow回归保留。Fork PR CI执行受影响模块；CI、部署、游戏实测和内存归因分别需要回执。

Capabilities描述源码支持和证据要求，不写固定实机/部署结论。公开任务目录 `packs/wvd/parameters/legacy-quests.json` 的59项由run_builder分派，未删任务。Fordraig/Cave工厂未被目录公开，保持休眠维护范围，不声称用户入口或实机通过。缺素材/dump/厂商SDK契约明确未确认。
