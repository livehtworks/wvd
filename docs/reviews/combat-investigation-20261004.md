# 巨人战斗技能异常调查

## 最新事故结论（candidate97）

- 用户随后授权继续战斗。candidate99正式调试入口使用当时已保存的“悬赏巨人”配置，run `574E5C9C-28BC-44A2-803D-CB806F16C471/1`于86.0675671秒Completed/quiescent，末次业务确认`dungeon_resumed`（frame156）；结束后只读新截图`next/.local/combat-continue-result-20261004.png`确认已回迷宫。该run的`execution-events.jsonl`为556671字节，接受的Auto/Pulse输入为零，所以本场通过不覆盖100ms Auto关闭可靠性。未修改用户方案、未启动循环、未继续返城。

- 用户后续要求降低点击间隔并立即检查颜色。当前实现为100ms，旧500ms在程序/门禁/报文三层拒绝；Pulse后的Await初始延迟为0，白色与黄色互斥判断，黄色只进入DisableAuto，2秒等待保留在已确认白色之后。不启动实机验证，不能据构建或受控检查宣称单角色隔离已通过。
- candidate99已部署17654，PID42000、实例`2A2BB9D8-0748-4E76-A111-331BB763F79B`，Idle/quiescent。原生构建、`test_native_author --single-auto-progress`和`test_native_devices`通过；正式只读识别接口对留存frame149识别黄色Hit/白色NoHit，对frame150识别黄色NoHit/白色Hit，仅证明颜色分类，不证明程序关闭成功。部署前后profile SHA256均为`3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C`；未操作游戏、未启动循环。构建/部署日志为`next/.local/auto100-build.log`及`auto100-deploy.log`。

- 日志修正版candidate98已部署原17654，PID27864、服务实例`53FCCAE5-8C4D-445E-8073-46FDCB07BD53`。原生构建、`test_strategy_frequency --semantics`中新增的识别成功但动作耗尽/真实低分/整组用尽分类检查通过，未运行全量测试。部署后Idle/quiescent，无游戏输入；不宣称Auto已修复。构建/部署日志为`next/.local/selection-log-build.log`及`selection-log-deploy.log`。

- 正常方案实跑run为`219C21C4-27A3-41FB-93C3-B662B46F5A23/1`。普修利catalog7迟缓攻击Lv3在seq485准备、seq532成功，不再走取消详情转Auto；友方技能也已推进，未记整场战斗通过。
- seq702的主角完整头像分数0.97350955、框`[87,55,73,51]`、ROI`[0,35,250,120]`，并非识别失败。其左上2级一次动作已在seq169成功消费，当前没有剩余动作；原reason错误写成`no_configured_portrait_above_threshold`。停止后新帧只读识别仍Hit0.97351015，证据`next/.local/auto-stop-current-20261004.png`。
- Auto脉冲分别在seq552/709发送，之后seq564/721发送补关。用户明确说明：已连续三四个角色被自动跳过，白色关闭是用户亲自点击，随后程序又点开。因此frame150白色只能证明当时状态，不能归因为程序补关成功；此前这项判断撤回。不能把发送双击或识别到关闭当作单角色自动隔离成功。
- 旧源码`src/script.py`的AutoThisChar调用两次同步ADB input tap，中间0.5秒，之后2秒；新版scrcpy路径仅复制间隔，没有证明相同输入效果。第二次未生效的底层原因尚未归因；本轮不改猜测间隔、不再操作游戏验证。
- 本轮日志修复将角色识别与动作选择分开：保留已识别头像/分数、剩余动作数，分别报告识别低分、已识别但无剩余动作、整组用尽、明确全自动和异常未选中。历史事件不改写。当前UserStopped/quiescent，不重开游戏或循环；无commit/push。

## 范围与现场

调查开始于用户暂停candidate93的run 2、要求查清爱丽丝debuff、狮樱单体转自动及柚奈误开烟雾弹。人工取消烟雾弹后没有继续施放；后续修复已由candidate95部署，见末节，正式方案未改、无循环、无提交。

运行证据：`next/.local/c11-flow-product/data/runs/25616F7E-B786-4BBC-8190-0AE5C338EE3F/2/result.json`。以下seq是该文件事件序号，不以当前配置代替历史执行事实。

## 已确认

### 1. 烟雾弹不是用户把柚奈配置成右上

- 人工取消后可见千紫万红在左上、烟雾弹在右上。原生坐标左上(266,965)、右上(640,965)，没有左右映射反转。
- seq765准备柚奈壬姬catalog 13，seq813结算success且consumed=false，5级重复行没有删除。当前正式方案该行也是左上、5级、重复。
- seq935在frame183却选择了艾妮琪catalog 4：艾妮琪分数0.807561，柚奈0.676657，阈值0.80。seq942实际接受了右上(640,965)输入。随后留存frame210及现场都显示柚奈壬姬的烟雾弹。
- 错误链已经确定为“以艾妮琪右上动作输入了柚奈技能页”，不是用户配置丢失或重复行耗尽。frame183原图缺失，尚不能区分识别当帧误认与换人过渡/输入延迟，不能声称已排除后者。
- candidate93的portrait取整图/右半/右上/上半四种裁片在顶部ROI的最高相关分，select只要最高角色>=0.80即可；prepared_actor再次用同一类模板判断，不要求跨帧角色身份稳定。frame210复核柚奈0.931、艾妮琪0.779，接近错误门槛但该帧并未复现Hit，不能拿它冒充frame183。

### 2. 爱丽丝debuff已开详情，失败在选敌

- seq618：爱丽丝_alt 0.974，选择catalog 3，即左下2级。seq629开详情回执确认；seq634进入Detail，seq638目标等级selected；seq641进入Missing，继而BackPopup→Auto→seq677 auto_confirmed。不是没识别到角色，也不是没有打开技能。
- Missing的条件是敌方技能详情成立但skill_target未命中。skill_target依次查NEXT 0.86、三角0.86、NEXT 0.60；未命中即转自动。
- 同场留存frame174的普修利单体详情使用真实生产recognition/probe复核：明细Hit 0.973，support absent为Hit，但NEXT最高0.332<0.60、三角0.685<0.86。ROI严格沿生产`[0,80,900,860]`，可见标记在范围内，不是ROI把标记裁掉。
- 原模板是白色NEXT/浅色三角，现场是红色NEXT/红色三角并有暗色及色散效果。此样本证明当前识别未覆盖该外观；爱丽丝frame122等关键图未留存，不将普修利帧冒充爱丽丝那一帧。

### 3. 狮樱的失败发生在详情打开阶段

- 两次prepare seq307/688，角色分数0.966/0.965，均正确选择catalog 5右上1级。各自执行三次右上(640,965)，然后OpenFailed→Auto，未进入Detail或等级、选敌分支。
- 第一组三次等待约856/854/851毫秒；公共combat-open-detail把“仍为战斗菜单且未识别到弹窗”也列作成功后置条件，故输入会先被记confirmed，外层又看到未开详情，重试三次后转自动。这是回执语义缺口，不能将confirmed解释为详情成功打开。
- 无frame59–64、133–138点击前后原图，不能确定具体原因是技能按钮不可用、当前快捷页不同、动画中点击未生效还是详情漏识别。附近frame57/145显示SP191/191，不能随意归因“缺SP”；这些附近帧也不能证明点击瞬间的按钮状态。

### 4. 自动保底会消耗一次性技能行

`CombatStrategy::consume()`同时接受Succeeded和AutoFallbackConfirmed，对“用完后移除”统一删行。例如seq143、seq926的auto_confirmed且consumed=true。这会让原技能未施放也被跳过；重复行则保留。需区分自动行动完成与原配置技能完成，不能用后者解释前者。

## 直接修复方向

- 行动者选择与输入前校验需承接换人过渡，记录获选与竞争头像分数、匹配框/裁片及动作时刻原帧；不能只提高全局阈值或再次使用同一弱证据。
- 已确认敌方详情内补红色标记或颜色不敏感且有形状约束的识别，点击前仍核对当前角色/详情；不能全屏低阈值盲点。
- 打开详情的后置条件区分“成功打开”“仍在菜单待重试”“明确不可用/角色已变化”，菜单仍在不应直接记详情成功。
- 自动保底与技能成功分别结算；这涉及一次性技能顺序语义，修复时同时检查方案结束规则。
- 对上述分支保存点击前、第一张结果及转自动原因原帧，而不是只依赖15秒周期截图。此次只有10张周期原图，缺失动作帧无法事后重建。

## 保留证据

- `next/.local/giant-smoke-cancelled-menu.png`：取消后新鲜900×1600技能菜单。
- `next/.local/giant-wrong-smoke-detail.jpg`：frame210原留存烟雾弹。
- `next/.local/giant-single-target-red-next.jpg`：frame174红色目标标记的单体详情。
- `next/.local/combat-failure-probes.json`：六项针对实帧的生产识别结果，未发游戏输入。
- 后续重新读取原`execution-events.jsonl`：文件实际为1138328字节，可解析完整事件，先前“长度为0”的记录不准确，撤回该判断。result仅为尾窗1024项；分析优先读取落盘事件。缺少动作原图与事件文件是否存在是两件事。

## 柚奈当前识别防护修复与部署

用户随后要求先修柚奈。当前稳定画面上旧生产识别器连续6张原帧均为柚奈0.928–0.933、艾妮琪约0.780，没有复现frame183的反常排序。源码核对显示角色参数进入结果缓存键，帧身份变化会清帧缓存，未找到串缓存证据；不据此排除缺图的历史瞬间。此前将“小裁片误匹配”直接说成历史事件唯一根因不够严谨，历史当帧/过渡问题仍未归因。

- 去掉portrait模式四个局部裁片独立取最高分，改为完整已裁好的73×51等角色头像比较。ROI仍`[0,35,250,120]`、阈值仍0.80，不扩大到排队头像，不提高全局阈值。prepare、prepared_actor及skill_target的角色校验共用同一实现。
- 当前原帧离线完整头像：柚奈0.9255，艾妮琪0.6170；旧右上裁片将艾妮琪提高到0.7816。证明局部授权削弱身份区分，不证明缺失的frame183一定由此造成。
- prepare日志增加portrait_evidence，包含每个角色的best_box/search_roi/crop/identity_basis及best_score，不再只保存分数；此次没有顺带改选敌、技能频次或正式配置。
- 原生生产识别器在当前菜单、烟雾弹详情、普修利单体详情及艾妮琪实帧的8项正反例通过，沿现有`test_native_author --roi-review`窄入口，日志`next/.local/yuna-portrait-check.log`。这不是整场战斗或全部角色验证。
- candidate94部署原17654、PID45532，实例`E1B442ED-61FF-438A-B2C3-0FBCDB1D2C5E`。部署前后profile SHA256均为`6D6E2D379B1AB386D41B39E091D6CFD1E7EA760221A3B11BA9DA179D35C635F2`。设备重连成功，任务Idle/quiescent，未发战斗输入。
- 部署后两张实时原帧：柚奈0.9268/0.9257，命中框`[87,55,73,51]`；艾妮琪0.6135/0.6171，NoHit。原帧及识别结果`next/.local/yuna-identity-20261004-160352/`，摘要`next/.local/yuna-deployed-scores.log`，日志依据均为full_portrait。
- 旧现场反常排序的确切原因尚未闭合；完整头像防护已生效，不能宣称历史故障已确定归因或不会复发。其他上述战斗缺口未在此窄修复中处理，整轮巨人任务未完成，无循环、无commit/push。

## 后续战斗修复与部署

- 技能详情打开后置移除“仍在原菜单”，同角色菜单内按1500ms间隔补点，原60秒期限不刷新；技能详情/资源提示/战斗结束/角色变化退出等待。外层不再套三次假成功调用，换人不消费原动作。
- 选敌沿同角色、详情、非友方、非确认技能门禁，在原彩色匹配失败时加三角灰度形状匹配，阈值保持0.86。红标记实帧灰度0.884，彩色0.685。爱丽丝原动作帧缺失，不能将此旁证写成其原帧复现。
- 自动保底确认推进动作代次并清理prepared，但不删除未施放技能，也不触发“任一完成结束方案”；只有技能success消费方案。日志分开action_confirmed与skill_confirmed。
- 选人、输入基准、第一张后继及确认帧送入同一历史图线程的四帧有界队列，关键帧保存无损PNG，周期帧仍JPEG；沿用240张/128MiB滚动上限。不额外取图、不在输入线程编码；队列丢弃/失败与queued记录可查，不保证磁盘异常时仍完整。

- 原生构建完成。沿已有入口执行：`--combat-open-repair`检查生产公共定义/执行器的原菜单补点、换人停点及真实RunStore的PNG无损/去重/退出落盘；`test_strategy_frequency --semantics`检查保底不删行与整组结束规则；`--roi-review`在红三角实帧及烟雾弹/技能菜单反例通过；`--giant-bounty`只读正式profile，编译2241节点巨人链通过。前三者日志分别为`next/.local/combat-open-repair-check.log`、`combat-frequency-check.log`、`combat-target-check.log`；整链日志`combat-giant-compile.log`。受控界面结果不冒充实机施放。
- candidate95已部署原17654，PID19788、实例`02E5346A-9456-4871-84B5-7F5B1177A2AF`；candidate94通过正式管理接口退出。保存的`combat-open-detail`只修改同一节点的原菜单后置分支和补点间隔，经原CAS接口保存，revision=`b7135cb351c0d68d0f490a9358c9fe5e665b7703fbbe56f02173911c387f6c59`，其余节点/位置/参数不变；原文备份`next/.local/combat-open-before-repair.json`。
- 部署后重新连接现有实例，只读两张实时原帧仍为柚奈0.927、艾妮琪0.614–0.615，完整头像命中框`[87,55,73,51]`。帧在`next/.local/yuna-identity-20261004-163420/`，没有发技能点击；任务Idle/quiescent，profile哈希仍`6D6E2D379B1AB386D41B39E091D6CFD1E7EA760221A3B11BA9DA179D35C635F2`。
- candidate95部署时未发技能输入；后续实跑见下节。缺失frame183及狮樱原动作帧不能重建，不把防护/回执修复宣称为历史瞬间已完全归因。回滚须先经manage_service停止candidate95，原CAS恢复备份公共流程，再启动candidate94；不可在旧二进制下保留新固定点击补点字段。无commit/push、无副本循环。

## 继续战斗与再起现场

### 单角色Auto事故与修复

- 人工把持续Auto用于面具单次保底，随后截图/工具往返期间已进入后续角色，未及时关闭；其后的关闭点击遇到复活页，未证明游戏开关关闭。此事故由人工操作造成，不能说成用户配置错误。撤回“单角色保底不能用Auto、应该改普通攻击”的无依据结论。
- 旧AutoThisChar明确采用开→500ms→关→2s等待；原生single_actor_auto此前改成启用后等待指令菜单消失，识别延迟可能扩大持续窗口。本轮恢复有界短脉冲，沿现有Command/InputGate/scrcpy通道增加仅支持500ms的双点击参数；关闭确认独立，剩余ON状态只补关，不重复开启，不改变全自动方案。输入日志记录click_count和click_pair_interval_ms，部分送达仍为unresolved。
- `test_native_author --single-auto-progress`覆盖菜单不消失仍关闭、初始开启只关闭、战斗结束不输入及两类调用者接线；旧测试配置未选择自身test方案，按当前可达方案契约修正后通过。`test_native_devices`验证双击触摸报文和非法间隔拒绝。日志`next/.local/single-auto-pulse-check.log`、`single-auto-wire-check.log`；这些不是实机成功证明。
- candidate96部署17654 PID1596，部署前后profile哈希均为`EBD64683CB356F06DE6915F77ABC5290ACD7C53FA29D525C93B2B1F1EC3CDF06`。未重启游戏/模拟器，部署后现场是满HP/SP的新战斗，Auto关闭；通过combat/debug重新执行最新方案，request_id=`83cc3284-802a-4df2-b976-5b358b362b33`。不运行副本循环，无commit/push。

- candidate95通过正式combat/debug入口继续当前巨人战斗，不启动副本循环。运行所有者实例为`27D295C2-9E96-4742-B486-E6E2ACA01063`，不同于service.json的服务实例ID；查日志不得混用。
- run1在爱丽丝友方技能卡片识别处停止：真实六张卡片可见，但support_selection只得到5个候选，结果ambiguous。原帧`next/.local/alice-support-live.png`及`alice-support-probes.log`保留。此缺口未修，不记为技能等级选择器失效。
- 随后的死亡圆环单次预测点击失败，出现“再起/接受死亡”。人工选择再起后重新进入同一场战斗，队伍HP/SP恢复；没有宝石购买操作。失败探针证据`next/.local/revival-live-click-20261004-164604/`，不得将其记为成功复活算法。
- 用户明确规则：右下角存在哈肯加护时可无限再起；没有加护时消耗命火，命火每4小时现实时间恢复一次，通常选择再起。命火与绿色/紫色宝石购买须分开处理；不把正常再起当成禁止的宝石消费。旧`src/script.py:2084/2233`识别RiseAgain后点击，再由RiseAgainReset设置复活后恢复、回退遭遇计数并ReloadStrategy；没有发现显式区分加护和命火的分支。现有原生revival工厂也未证明这两种真实界面都已覆盖。
- run2已确认施放柚奈、爱丽丝、面具、普修利、艾妮琪的动作，随后狮樱右上未打开详情，公共流程超时。当前帧狮樱完整头像0.982，右上人工点击同样未开详情；不是本帧认错角色。用户随后确认自己将狮樱误设为右上，已保存为左上1级、重复。人工确认左上“残花”成功，SP191→179；没有替用户改配置。
- run3使用用户最新保存方案继续当前战斗。新debug会话重新建立本场策略账目，不是恢复run2的已消费动作账目；整场胜利尚未确认。
