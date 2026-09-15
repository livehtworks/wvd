# 专项任务源码语义清单

来源为固定 `6585f4075f5714ab522aa582993860c09af912c1` 的 `src/script.py::QuestFarm`。
这是迁移依据，不是完成报告；实际实现和测试状态以 `m4-task-status.json` 及专题报告为准。
不执行旧 Python，不将下述步骤转换成一个通用脚本解释器。

## 基础目录中的十五项

| 原 TaskID / 源码行 | 不可遗漏的主链与分支 | 计数或特殊边界 |
| --- | --- | --- |
| fortress-B8F_trap / 4365 | 七个目标：一层楼梯、mark_auto、四个固定点、要塞门楼梯；不回城 | 每轮开始增加旧次数，完成数另记；同轮恢复重入局部路线，不重复开始计数 |
| 7000G / 3472 | FortressArrival 跳跃、返回要塞、王城、公会剧情；依 stepMark 顺序选 illgonow、olddist/iminhungry、royalcapital、三处剧情、leavethechild、icantagreewithU、illgo、noeasytask，最后确认 ruins | 开始计次；7000G 收益只能在整段剧情终点确认，不在看见选项时计入 |
| manualSepDemon / 3803 | stair_2→harken；两次返回、住宿；DHI/BeautifulOre 跳跃；覆盖局部 EOT 为 COS/COS→COSB2F；stair_3→固定点 | 一次性链，不包装成无限循环；各段局部路线和来源原树分开 |
| darkLight / 3680 | 本内循环：未知检测、战斗/宝箱、计时结算、按开箱/战后配置恢复、darkLight→darklight_lightIt；返回旅店结束 | 不套普通入本/任务点路线；没有普通 dungeon 开始计数；保留 400 秒无进展检查与未知诊断 |
| LBC-oneGorgon / 3827 | GhostsOfYore＋symbolofalliance 因果（EnaWasSaved，RGB=2/1/0）、返回要塞/王城、住宿并领任务（偏移266/257）、牛洞 | ACTIVE_REST 真：第一牛→退出→住宿→再入→第二/三牛→退出；否则三牛连打；每轮开始计数 |
| SSC-goldenchest / 3889 | specialRequest/SSC Leap、王城、住宿；专用领任务：一次大滑动、细滑查找、偏移300/150；进洞、关闭陷阱、七个地图目标 | 不是通用 StateAcceptRequest 的三次大滑动；dotdotdot/shadow 专用对话；宝箱 ROI 排除区原样保留 |
| jier / 4289 | requestToRescueTheDuke 跳跃、要塞、王城、悬赏揭榜、B4FLabyrinth 三点、回公会交付、离开菜单、按专用间隔住宿 | bounty/cuthimdown 专用对话；交付只一次；开始次数与交付完成不能混同 |
| Scorpionesses / 4074 | 三种跳跃入口选择、揭榜、B2FTemple 两点、回公会交付、离开菜单、按专用间隔住宿 | ACTIVE_BEAUTIFUL_ORE 优先于 ACTIVE_TRIUMPH；前者直接走 DHI 分支，不补造王城传送 |
| Scorpionesses_plus_6_hands / 4155 | 蝎女主链后，另进 B5FWarpedOnesNest 两点，再回公会；两个 CompletionReported 分别交付，每次都退出菜单 | 不能只复用一次交付，也不能一见同名按钮就连续点两下；每次重新观察 |
| gaintKiller / 4045 | 局部 EOT 要塞七层、固定点560/982、harken2、返回旅店、按专用间隔住宿 | 源码已经跳过巨人检测，默认灯怪路线；保留 gaint 拼写；不新增旧源码已不执行的分支 |
| lovesleep / 4353 | 连续 9999 次完整 StateInn；每轮前后检查停止 | 不受普通“已住宿”回执永久抑制；每次独立住宿周期；不能把现有256段上限当成删减9999功能的授权 |
| FFXI-Org / 4385 | EOT→自动到矿点→小地图确认→挖取循环→无矿/无镐子退出；无镐子则 RTT、重组FFXIStone队伍、真实住宿补镐子 | org_position ROI=[692,68,140,140]；领取前后都检查；十种奖励最高分且>0.9，否则未知；resetBag 只能住宿完成后清除 |
| sandman / 4521 | 禁用组队刷新/空气墙；局部要塞三层 EOT、楼层检查、四点；三项专用对话 | bondmate 回调后才住宿→requestToRescueTheDuke→等待10秒→住宿→Triumph→完成计数；路由完成不等于缘完成 |
| fishing / 4574 | 近端抛竿4000ms、补饵、浮标、收杆、鱼获结果、未知页安全转向 | 见下方钓鱼完整边界；不把浮标 NoHit 当已经获得鱼 |
| fishing2 / 4574 | 同链远端抛竿2250ms | 保留独立 TaskID/模式统计，参数化差异，不复制第二套执行器 |

## 共用但不等价的环节

### 领取与交付

`StateAcceptRequest`（3397）先找旅店并住宿，再进 guildRequest/guildFeatured，三次
150/1000→150/200 滑动，细滑查目标，检查 request_accepted，未领取才点目标偏移，最后
返回旅店。SSC 使用自己的领取流程，不能一概替换。源 request_accepted 的 ROI 参数
含位置相关高度，须按旧裁剪实现再核对，不猜测为上下各200像素。

悬赏揭榜和交付都带公会菜单的横向滑动后备。CompletionReported 是交付输入，不是
看到它就确认已领报酬；窗口不明时需要保留副作用待确认事实。

### 时间跳跃

`CursedWheelTimeLeap`（2012）先尝试已可见目标，失败才重置章节页签（十次向左）、
选择章节、三次大滑动及细滑。ACTIVE_CSC 关闭时不调整因果；启用时先关闭现有因果，
再设置调用者提供的选项/RGB。滚动结束用指定 ROI 前后像素差 <0.006，而不是猜固定页数。
持续 leap 页会再次尝试目标。未知状态中的 cursedWheel 分支另有 7300 秒等待或
转7000G消息（不是同一个跳跃函数）；须用有限段与真静止后任务交接承接，不能扩成无限Session。

### 计数与住宿

巨人、蝎女、蝎女＋六手、吉尔和钢试炼使用 `(开始次数-1) % (REST_INTERVEL+1)`，
不同于普通 dungeon 的 `max(REST_INTERVEL,1)`。负数、除零和最大整数边界需明确拒绝或
报告旧语义冲突，不能偷偷套普通间隔。沙人两次住宿之间有跳跃，不可被上一张住宿回执吞掉。

### 钓鱼

无饵必须同时比较 nobait 与8bait分数，ROI=[530,1469,120,120]，避免把8识别成0。
补饵需退出钓鱼、地图点818/928、物品列表、鱼饵转交至baitbox（最多70次，每次可停止），
返回旅店、局部EOT DH→DH-R6、点339/555、重新开启钓鱼并等待10秒。转交也是资源副作用。

抛竿前五次正向/两次反向100ms滑动，随后按近端/远端时长抛竿，等待10秒。
等待超过300秒时点击striking、记失败并重新截图；否则在ROI=[250,500,400,600]识别浮标，
空列表才收杆。CloseFishInfo 点击后记一次鱼获；规格、种类分别最高分且>0.9，种类未知
记“未收录”，规格未知不编造分类。不能重复识别同一结果页就重复计数。

未知页只在dungFlag视角允许转向，40次转向每次重新检查钓鱼页/普通阻塞/本内锚点。
其它未知页90秒后请求恢复。原静态浮标算法通过不等于这些流程通过。

## 源码额外 case

源码另含 `fordraig`、`repelEnemyForces`、`CaveOfSeperation`、`steeltrail`，不在冻结的58项
基础任务目录中。它们仍属于源码语义盘点，不能删源码索引，也不能伪造四个基础TaskID改变
分母；需检查显式mod目录是否提供对应定义并登记扩展承接状态。已确认的关键差异：

- fordraig：特殊跳跃/领任务、四层机关和Boss前后 SYSTEMAUTOCOMBAT 开关。
- repelEnemyForces：不包含接任务/跳跃，REST_INTERVEL 控制两连战组数，ACTIVE_REST关闭时强制1。
- CaveOfSeperation：分层变更对话及强制停点；EnaTheAdventurer/requestwasfor 停点不能继续盲点。
- steeltrail：公会gradeexam、Steel偏移306/258，ready/noneed/quit对话，四点及专用休息间隔。

钢试炼已构建，状态及计划检查通过，专项流程复验中，见`../m4-steel-trial-validation.md`。
击退敌势力已接入独立阶段对象和双战终点，待构建及离线验证，见`../m4-repel-forces-validation.md`。
fordraig十段与CaveOfSeperation六段已实现并接入状态所有者、CLI、共用封存发布及离线入口，
当前构建和验证未完成；分别见对应专题及`../m4-stage-publication-validation.md`。
分段保持同一个Run/资源revision，各段冻结独立对话策略；上述接线不改变基础58项分母。
没有为这些额外case开放真实操作许可，也没有登记完整离线验收通过。
