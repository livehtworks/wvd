# 第9轮反复重启现场排查

## 当前状态

故障批次部署为输入hash `d1b45197726826370f8ad9605ab196d389b4b9f8434124843882935717aa697a`，服务实例 `E398993B-FFA7-4516-8331-0819EDF84AF5`。批次 `fa827b34-27bc-40cd-8636-41bd65c3d6c7` 已完成8/96；run9停在住宿后的离店/启动恢复链，非第9轮完成。当前修正版部署见文末。

发现重启循环后已通过正式停止接口取消run9及循环；当时API为UserStopped、busy=false、quiescent=true、repeat.active=false。未关闭模拟器，未清日志/图片，未更改用户配置。以下修复已完成源码、限定验证及部署；用户随后明确授权提交/推送/部署和新100轮，当前状态见文末启动记录。

## 已证实的问题

run9事件文件记录6次 `diagnostic.application_restart`，seq为9454、9759、10021、10273、10528、10783，均为 `CONTINUOUS_EXCEPTION_TIMEOUT`，关联 `Task_Inn_Leave`。随后恢复记录均报告application_restarted=true。首次还经历METADATA_TIMEOUT、DEVICE_INSTANCE_RESTART_REQUIRED及设备重连，不能说始终只做了简单游戏前台切换。

第一张重启前图片 `diagnostics/1.png` 是正常“背包已補充完畢”提示，继续箭头约在(840,1136)，不是网络故障或宝石购买。`next/native/games/wvd/supply/inn.cpp` 中supply_arrow限定 `[775,940,125,120]`，下边界1060，不包含真实箭头。ContinueSupply的场景和目标均依赖此条件，因而无输入可执行，正常提示被留在未知等待中，60秒后进入游戏重启。

第2至第6张重启前图片已是要塞城市。进一步逐事件核对：seq9708–9712实际经过ReconnectBoot_Ready、ObservationPhaseEnd及Terminal；seq9713回到Task_Inn_Leave，seq9714记录EVENT_RESUME_UNCONFIRMED。生产Service在原图6上也命中AtCity和完整Boot Ready条件，blocking_screen为NoHit。因此不是缺城市素材或Boot识别没有结束。实际缺陷是FlowExecutor::resume_event先检查旧父节点的超时，再检查新画面的resume_guard，已恢复城市被旧Leave期限拒绝，导致重复恢复和重启。

FlowExecutor在连续异常期限到达后先复核当前观察，再发bound_game.restart；游戏重启后保留父业务栈，推入ReconnectBoot事件。此次父栈为Task_Rest→Task_Inn_Leave。启动处理已收敛，但恢复接续顺序拒绝了有效新画面，导致同一业务停点反复重启；不是正常循环每轮重启，也不是本轮WVD_CONDITION_DEPTH复发。

## 保留证据及后续边界

证据根：`next/.local/c11-flow-product/data/runs/E398993B-FFA7-4516-8331-0819EDF84AF5/9/`，包括result、execution-events、action-timing、diagnostics及六张重启前原图。最新正式发布程序在 `data/published/repeat-ae9c571af2f4d741a3adededc53aac360b9cc62562c64b149a7a9b29176b59e0/`。

## 修复范围

- 对照旧src/script.py的StateInn：选择房型、确认住宿，返回旅店菜单后PressReturn；没有等待背包补给信息或满血的步骤。新版确认旅店菜单后使用ADB BACK，菜单退出即结束住宿子步骤。上层有住宿回执时不重复等待城市/补给文字；未住宿路径仍保留城市判断。
- 剧情/补给信息出现才继续，不要求出现，不读取道具名称或“补充完毕”正文。继续箭头及住宿确认搜索整个下半屏；離開名称搜索菜单下部，不依赖狭窄的固定高度。保持原阈值、实际匹配中心、网络遮挡和宝石购买保护。
- Boot增加可选正常信息页处理，但不接管已知迷宫奖励页；故事仍要求AUTO控制和无选项，不把任何黄色箭头都当故事。
- 恢复接续先看当前resume_guard，再在NoHit时检查旧节点超时。未决输入仍逐项核对原后置、身份和送达状态；不清回执、不盲目重放。
- 同页返回键补试只放行KEYCODE_BACK=4，不能扩展成HOME/POWER等系统按键重试。沒有调整60秒异常窗口、重启次数、用户配置或付款安全边界。

## 限定验证

最终源码限定验证均通过，日志在next/.local/architecture-audit-20261010/：

- inn-exit-final-binary-frames.log：最终测试产物的生产Service使用原图1/6和真实冻结包，完整ContinueSupply/Boot信息页在提示图命中、在城市不命中；完整Boot Ready在城市命中。没有Error。
- inn-exit-fixed-fixture-contract.log：可选提示有/无、BACK菜单退出后没有城市帧仍完成；旧Leave过期后恢复到城市只接续一次，无重启/重放/未决输入。
- inn-exit-verified-transitions.log及隔离同名证据目录：既有住宿pending加载等待、城市不伪造付费、金币房型重办语义保持。
- inn-exit-boot-progress.log、inn-exit-boot-revival.log及对应独立证据目录：启动进展/网络补试、再起交接与未决输入保护保持；inn-exit-chest-handoff.log保留战斗/宝箱交接、目标归属和无路线停止。
- inn-exit-final-contract.log保留一次夹具失败：故事箭头ROI扩宽后与信息箭头成为同一条件，原夹具错误地将非故事场景的箭头强制设NoHit。已只修外部观察夹具，未改生产逻辑/放宽断言；最终日志为fixed-fixture-contract。

受控观察只用于复现业务分派和恢复接续，不冒充真实游戏一轮；原图复核只证明对应页面识别，不制造实机异常，不启动新轮。长期循环稳定性尚未证明。

## 部署收口

- inn-exit-product-build.log及对应.local/logs/inn-exit-*记录Web/原生构建、依赖/冻结输入和打包成功；inn-exit-service-stop.log、inn-exit-service-deploy.log记录旧自有服务退出及新版就位，没有关闭模拟器。
- 原17654/原data-root；新服务实例3891BA20-7485-49CD-B15A-60D58E90F34D、pid22400。网页HTTP200，服务API正常，当前Idle、busy=false、quiescent=true，未启动循环。
- product_inputs_sha256=f068d3cb93428fae31df90f3905afa0781a59e638396150ac658595aab3da33d，1289项；EXE=8eae4ed0b3c351f6cc4655958cec21edd4c95be5fde31bfa819661d393b2492e。source_commit仍fd6b617，包含未提交修复，不将旧提交号冒充全部本轮源码已提交。
- profile SHA256仍3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C，revision仍d26988cd440cd5f55fbf2ca34e0e4b7b1aaf54dc16d1717d154e0a065b865cab。没有改用户配置或保存流程，没有新增实机轮次、付费或内存采集。本次源码/限定验证/部署完成，实机整轮与长期稳定性未验。

## 提交后100轮启动

- 用户再次明确要求commit/push/部署并运行100轮。已将上述修复及前序页面交接/补给/部署修复提交91f49112ac271c0e297c1e2fa608964426586374，推送个人fork的agent/local-stability-notes；未提交用户.vscode。产品输入与已验证产物一致，没有重复跑全量验证。
- commit100-product-build.log、commit100-service-stop.log、commit100-service-deploy.log记录按91f4911构建、打包和部署；product_inputs仍f068d3cb93428fae31df90f3905afa0781a59e638396150ac658595aab3da33d、EXE仍8eae4ed0b3c351f6cc4655958cec21edd4c95be5fde31bfa819661d393b2492e，1289项。新实例2F1D2172-8C75-47AB-B643-898E3800C6CD、pid21016，原17654/data-root。
- 正式/api/v1/runs/start请求6ed1c35c-b9a3-4811-846e-90681e2e2a57：GiantBounty、resource_locale=zh-Hant、repeat=true、repeat_count=100，提交时核对当前profile revision。不是续接旧计数；原跨批12轮不计入新100轮。
- 已核对实际Running/run1/0完成/目标100及repeat.active=true；输入回执确认跳轮、进入公会、委托和悬赏页面、退出列表及进入副本。真实近期图083700376_r1_f19.png为郊外/第十區-要塞3F領主室列表，随后进度Task_FirstDungeon_Route0_Moving。检查时未出现错误、失败诊断或游戏重启，不能将此记成完整一轮/100轮成功。
- run证据在data/runs/2F1D2172-8C75-47AB-B643-898E3800C6CD/1，execution-events.jsonl、action-timing.jsonl、recognition-memory.log正在保存；data/recent-frames持续保存动作PNG/周期JPEG。profile哈希/revision不变，AUTO_START_CLASH=true；未改VPN/战斗配置、未新增系统内存采集，禁止宝石购买/抽卡/卖装备的边界继续有效。

## 战斗分阶段识别修复

上述100轮完成7轮后，用户要求处理识别成本并重开。正式停止确认Completed、busy=false、quiescent=true、repeat.active=false，未关闭模拟器。旧六轮实际模板比对27508–65594次/轮，不是不同素材数；同类节点没有持续递增证据，明确冗余来自复合条件在已判正常战斗后仍展开其它页面。

- combat_phase作为现有WvdVision的游戏阶段配方：先当前battle，再按clear/menu/detail_handoff或实际dungeon/chest/revival出口识别；技能结果finished仍排除详情/确认/关闭及资源错误。通用all/any、输入epoch/身份、未决输入、Auto与业务结算不变；不增加等待，不降低阈值，不用旧帧长缓存。
- 模板闭包按phase登记全部可达资源，模型闭包按实际locale解析内部再起/资源错误OCR；不能借动态跳过某页放行缺模型。没有新增执行器或全图场景扫描。
- phase-current-battle-frames.log使用真实第7轮菜单/详情、旧奖励及黑屏，和完整冻结包/生产Service/真实业务状态比较新旧结果，均相同。菜单ended实际比对7→1，详情finished14→2，menu5→2；奖励finished38→26，黑屏仍NoHit。未测热缓存耗时来宣称实机提速。通用all缺图/无效phase仍Error，只有phase finished的独立图缺隐式OCR模型也拒绝发布。
- phase-final-handoff.log、phase-open-repair.log、phase-reentry.log为受影响交接、同页补点/转场不补点与遗留详情关闭的限定回归；没有新增全量测试或制造实机异常。phase-real-frames.log保留夹具缺业务状态错误，phase-real-frames-final.log保留隔离图未建立Entry错误；只修夹具装配，未改生产逻辑迎合测试。选图时核对旧diagnostics/10.png实际是黑屏，只作反例。
- phase-product-build.log、phase-service-stop.log、phase-service-deploy.log记录构建、打包及部署。产品输入fc2e5efa73c736084f502410ed54d50d304ce6b2371a49e3a9986121f4fe3b5d、1290项、EXE b6d4dcc3fe12c6d73563222499fa60c13fd3ae71cfc44429b20e0bc7ecf5ce9a，基线提交8d3547c含本轮未提交修复。新实例5B891D68-1CC7-46F0-9E06-3738B430DF4F、pid48448，原17654/data-root。
- 正式公共步骤准确备份在phase-saved-before；只改open-advanced/result节点condition，经原API revision CAS保存并逐对象核对其它内容相同。combat-open-detail revision由6c070bec191899ab48bf80df83c913ec441c71c3d73d8b0411aa248ad01712ac变为0227a8b31eb5ed90bb6e3340a31658ca9c074d90bae1f123125a2c34eac03dbc；combat-confirm-result由768cf5b95ea0b97476c5d47b5d59e837eb14a67571be348d44b904dbf7c8482b变为456ac3a7bff5391fd1df9d74e8562be05a7e134eae10434b05d9819e812c9e59。
- 用户授权剩余93轮续跑，正式请求5f4e827e-8454-41de-abe6-e6d7fe5b2959、GiantBounty/zh-Hant，已确认实际Running/run1/0完成/目标93及repeat.active=true，不把accepted当开跑。旧7轮与新93轮分别计数。profile哈希/revision、战斗配置、VPN与日志设置未变；实际整轮成本仍待日志观察，不预报长期稳定性通过。

### 实跑补齐及覆盖页出口

- 首个候选实跑Entry平均330.4ms/35.5次比对，result平均236.4ms/11次；打开详情等待仍约1101ms/59次，查明open/target输入后置仍展开旧大条件。已同步源码、发布资源与正式保存open/target后置；最终保存revision分别为9fb4909c5e6db0c2c1d53af02584445b2bfe44a8283bb7276953663c4a8cbe2b、cc7002f7d75ae8288c64139e7c4ca08884659f88da7a87223eb635a2be69e085，原定义备份在phase-post-before，其余内容逐对象核对未改。
- 完整后置候选实例25DD8A42-B34E-4C90-B42F-960C150B0E58/batch de9e5f65-2049-4b93-a67a-cab4f3b7037c已实际执行战斗，随后在Task_FirstDungeon_Heal_Requested卡住：diagnostics/1.png为满宽HP条/右下关闭按钮覆盖页，补给和Boot不认识该页；记录两次CONTINUOUS_EXCEPTION_TIMEOUT重启。首次卡住前最后输入为技能流程，没有确认补给输入，进入该页的业务原因尚未证明。已人工停止、0/93，不计部分轮。
- 【已撤回错误实现】hp_overlay把顶部绿色条/右下X当普通可关闭页，新增Boot及补给关闭出口；实际目标中心(807,1489)，score0.850670。这张图实际为救人过程，操作语义错误，不能因匹配成功或未记回复成功就称安全。该实现及支持它的旧测试已撤回，详情见下方T0事故。
- phase-full-chain-frames.log记录最终打开/选人后置的新旧语义/成本核对；动画detail_handoff13→2、详情57→11，详情target_handoff89→32，未知黑屏不完成。定向open/reentry/handoff通过，不扩大全量测试，也不宣称整轮耗时已获最终结论。
- 日志没有丢失：停止后result的action_timing=1660行/1970719字节、event_history=3671行/6366363字节、diagnostics=40行/66669字节；各failed_rows=0/complete=true，details_complete=true。活跃目录枚举曾显示0不能作为空日志证据。未新增系统级内存跟踪。
- hp-overlay-verified-frames.log的旧PASS仅证明错误的关闭链能够执行，不证明救人业务正确，不能作为放行依据。该正向断言已由撤除断言替换；原日志及失败日志全部保留，不重写证据。hp-overlay-heal/open验证仍仅对应其限定合同。

## T0取消救人事故与撤回

- 用户明确指出救人页被取消、角色已死亡。源码fedfbf5新增Boot_CloseHpOverlay抢在正常分派前，运行实例D6857E39-8C97-4CD5-BBEE-F180C11FADA9/run1的execution-events.jsonl：seq13为点击(807,1489)尝试，seq15为accepted，source_path=Boot_CloseHpOverlay。不是手动操作，也不是旧救人逻辑。随后战斗截图可见狮樱HP0/598；目前证据证明取消救人，不能证明永久损失或倒推首次死亡发生时刻。
- 原recovery/party_death.cpp分支仍存在，确认救人页后点击(450,800)、逐帧继续或接续再起；本次没有修改它。事故原因是新增关闭分支绕开了既有救人业务分类，错误验收只检查按钮命中/关闭而没有核实页面用途。
- 循环已停止：UserStopped/busy=false/quiescent=true/repeat.active=false，0/93；错误后台pid10856已经manage_service退出，模拟器/游戏没有由撤回操作重启。
- 按用户要求完整移除Boot/补给CloseHpOverlay、hp_overlay识别模式、关闭配方、编译依赖和作者白名单。保留combat_phase性能改动；不重写用户profile/保存流程，不盲点，不自动开循环。旧正向关闭测试替换为真实事故帧的已撤销模式必须Error且不可授权输入、编译图无关闭分支及既有救人中心保持的定向验证。
- 事故PNG/日志留在本地忽略目录，历史错误提交保留，通过新提交明确撤回，不重置历史。救人实机可靠性仍需另行核对，撤除完成不冒充救人或完整循环通过。
- 撤除验证t0-cancel-final-revoked.log通过：生产Service对事故帧上的已撤销mode返回Error且没有目标坐标，Boot/补给编译图不存在取消入口，救人中心仍(450,800)。t0-death-transition.log既有再起/战斗接续及取消停止合同通过。首次t0-cancel-revoked.log错误要求Error结果的独立action_eligible标志为false；实际输入合同要求Hit和可操作同时成立，改为核对Error与无目标坐标，未改生产Service或门禁。
- 发布包回滚：旧完整候选wvd-next-native.previous-c81d563d5ea9475d96fc1bf70ec9f56b经manage_service Validate通过；确认错误后台退出后在同一dist根内原子改名，将错误候选移到wvd-next-native.t0-withdrawn-fedfbf5，旧候选还原为wvd-next-native。没有删配置/日志/资源、没有兼容旁路或自动回退。当前输入hash81fd113333cd13ad02c62e664fec527d4d2733d3682937eaaa3829cb76521bd3，EXE1b58ef43f65b851ed0e8365cdcf070566d665466f7c5e38219af77d39d45e822，构建来源8d3547c，保留原身份。
- t0-rollback-final-deploy.log：实例23FC3DF9-FE96-460E-8A5D-049909751C81/pid58992正式17654/原data-root。API回读Idle/busy=false/quiescent=true，未提交start，profile哈希未变。第一次改名命令在解析阶段失败，后续部署曾重新拉起原错误服务但未启动任务，已立即再次退出后重做改名/部署；此工具编排错误保留日志，不能把那次服务启动当回滚完成。

## 撤回后同页多处理器只读审查

### 已证实的救人调用缺口

本节审查源码356ba5a、当前回滚产物、已保存公共定义及事故前冻结程序；不改生产源码、配置或运行数据，不构建、不测试、不操作游戏。结论不把“多个调用点复用一个工厂”算成多套实现，也不把同一条件配置不同技能/角色点位算成冲突。

- 既有救人只有recovery/party_death.cpp的中心点击处理；shared X并非新增发现。native_recognizers.cpp:1690附近原注释已明确救人也有X，通用X只能排除Pause，不能证明普通角色面板。新增hp_overlay恰恰违反了这个已有边界。
- supply/dungeon_recover.cpp:10的继承列表只有network/pause/download，没有party-death/party-defeat；Entry只有Unneeded/Encounter/ResumeAttempt/Requested。vision/supply_scene_plan.hpp的Interrupted表包含战斗、宝箱和RiseAgain，却不包含救人/多人死亡。故救人打断补给时，既不能正常交接，也不能沿已存在的救人事件处理。
- runtime/flow_executor_events.cpp:70-73实际按子定义inherit过滤父规则，不是仅在文档中少写一个名字。事故前冻结repeat-ae9c.../program/flow.json的Task_FirstDungeon_Heal_Entry也确实只继承三个事件，本地Death步骤及本地事件均为空。
- recovery/boot.cpp:136-137的common=false候选没有Death/Defeat，210处只为common=true构造救人子图；ReconnectBoot_Entry在同一冻结程序中inherit=[]、events=[]且没有Death步骤。恢复进入这层后同样没有救人出口，这是第二个已证实漏接。
- 必须区分冷启动与重连：with_boot_recovery根定义仍注册wvd-party-death，冻结Entry/Boot_Poll亦保留父层事件；因此不能笼统声称所有冷启动都没有救人处理。问题是子调用继承被过滤，以及Boot自身的本地候选覆盖不完整。
- 这些证据证明了处理器不可达，不证明someonedead模板在事故图上实际NoHit或某个匹配分数。旧日志未提供足够的该叶子分数；本次不杜撰识别失败原因。新增关闭分支的错误动作则已有seq13/15直接证据。

### 六类已定位的独立处理定义

以下为已确认的下限，不是已穷尽全部作者自定义流程的总数；不同后置/次数/延迟不一定都已经造成实机事故，但确有多处独立编写同页动作，修复一处不会自动更新另外几处。

| 同一页面/动作族 | 独立定义位置 | 差异及风险 |
| --- | --- | --- |
| 下载确认，4处 | boot.cpp:224的Boot动作、379的handle_download、navigation/time_leap.cpp:127、public-flows.json的wheel-open/Download（1148附近） | 共用下载素材，却各自定义动作/后置/次数。Boot延迟1500ms且max_hit6，事件直接结束，无Boot该延迟；公开步骤repeat_limit32。不能只修事件处理器就宣称所有下载链一致。 |
| Pause恢复，2处 | boot.cpp:254附近的ResumePause循环、397的handle_pause | 同为pause/(450,760)，Boot后置recognized与事件response不同；Boot设置hit_limit6，事件Press节点没有同样显式设置。共用探针不等于共用输入流程。 |
| 普通剧情继续，3处 | boot.cpp:183、414的handle_story、supply/inn.cpp:87 | 同为ordinary_story_page及story_advance_arrow，Boot等待story/ready，事件接受城市/选项/迷宫/战斗，旅店等待after_payment；前两者延迟2000ms，旅店本地没有这一设置。 |
| 旅店退出，2处 | boot.cpp:163/166、supply/inn.cpp:97/102 | Boot点繁中離開或Stay偏移，要求城市/角色页后置；旅店走Android BACK，菜单退出即终点。不是相反动作，但同一退出职责由两套入口/完成条件承接，已有容错修复可能只覆盖一边。两边还有分别声明的CloseCharacter。 |
| 旅店补给信息继续，2处 | boot.cpp:177、supply/inn.cpp:90 | 同用inn_notice_page/notice_advance_arrow；Boot延迟300ms、后置notice/ready，旅店hit_limit6、后置after_payment。都能处理相同页，但重试与完成语义独立。 |
| 技能弹窗取消，3处 | combat/auto_combat.cpp:31/36、82/87、combat/turn.cpp:120 | 单角色Auto和持续Auto分别复制X/左偏取消/BACK逻辑；普通回合重入只在技能专用detail下BACK。两种Auto功能本身不同且必须保留，但弹窗清理不应各自扩大分类范围；不能把正常确认/选人混进取消。尚未证明当前三者在救人图上都可授权点击。 |

另外一类重叠风险：boot.cpp:44的网络重试子图用正文/按钮及英文0.86阈值，234-236的Retry/RetryLow又直接定义英文普通/0.60阈值及固定(450,900)点击。不是两个独立网络引擎，但相同英文网络页可落入不同识别门槛及动作路径，繁中当前不能直接外推发生过此冲突。列为待统一，不算已证明T0事故。

### 不属于重复实现的部分

- public-flows源20份、正式保存25份；源/pack/回滚部署pack逐对象相同。7份保存定义初看有差异，按node ID及边的from/to/outcome/order规范化、排除布局/语言默认值后，当前20份同ID公共定义均无动作语义差异。不得据原始数组顺序差异覆盖用户配置。
- 事故前冻结巨人程序中，同scene/target但command不同的精确分组有3类：四格技能入口、六个开箱选人点位、补给轮流打开前排角色。这些是明确参数化选择，不是三起重复分支事故，也不是三套执行器。
- 多任务调用dismiss_party_death/revive_after_defeat/choose_default_dialogue，以及节点展开后出现许多同名前缀，是共享工厂/公共定义，不按出现次数计套数。蝎女/巨人任务的路线差异及单角色/全场Auto策略也不能借清理重复擅自退役。
- src/script.py为参考；旧IdentifyState在someonedead命中后点中心，不点X。本次没有旧Python并行运行的证据，不能把参考源码算成另一个有效执行所有者。

### 后续整改边界

优先补回既有救人在补给/重连的可达性，并核对其实际场景识别；不得再新增取消或第二套救人处理。然后按页面职责统一上述重复动作配方，启动、事件和业务调用同一处理器，只保留调用者自己的业务终态判断。迁移须承接相关任务、公共保存定义、回执和恢复；不能通过放宽共享X、删分支或整体覆盖保存流程“统一”。本节没有执行这些整改，当前循环继续停止。
