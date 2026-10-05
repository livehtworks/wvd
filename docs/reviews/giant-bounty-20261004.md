# 第三章巨人悬赏

## 当前实机结论（candidate100）

用户要求继续整个后续流程后，正式GiantBounty入口从战后迷宫续接。candidate99的run `574E5C9C-28BC-44A2-803D-CB806F16C471/2`已经到达哈肯，但标题“移動”框[36,211,76,30]被旧ROI[0,230,250,170]裁掉，Moving未选中Retreated，60秒异常重启后ADB_TRANSPORT_FAILED，实例进程随后确认已退出。失败原图在该run的`diagnostics/2.png`；实例退出原因尚未归因，不能把它说成未成功导航。

修正原生共享哈肯探针和作者语义素材：标题ROI[0,100,250,400]，归还ROI[200,400,500,1000]，保持双锚点及原阈值。原失败帧旧标题NoHit、新标题Hit，组合Hit；candidate100正式语义接口命中标题和归还。构建/部署日志`next/.local/harken-layout-*.log`，部署PID13320，原17654与原data目录，未改正式方案。

最终run `1D1ACB0C-936D-4E8F-A939-EF0EF5F58037/1`为Completed/quiescent：先快捷返哈肯、要塞提交旧报告、住宿，再第三章凯旋跳轮、悬赏刷新、不落要塞第十区、标记快捷导航、巨人战斗、3秒等待/战后恢复、快捷返哈肯、要塞提交、200G住宿、退出旅店。整次527.03秒；从giant_bounty_started到bounty_cycle_completed为342.82秒。完成轮数1，报告2、住宿2分别对应旧战斗收尾及新一轮；没有启动重复循环，没有Auto输入，不宣称Auto分支已实测通过。

关键证据均来自该run的`execution-events.jsonl`（4646438字节）：旧报告完成seq1229、旧住宿完成1397；新轮开始1551、跳轮完成1695、悬赏刷新1866、入本1971、战斗开始2188；战后恢复迷宫3668、AfterTargetBattle0等待3674到Confirm0为3.545秒；返城3922、报告提交4096、住宿4270、整轮完成4436。两次哈肯归还接受输入seq980/3830均在匹配中心[453,1051]。完成后新截图`next/.local/giant-full-cycle-end-20261004.png`复核为要塞城市。未commit/push。

## 业务链与权威

- 目录新增`GiantBounty`，名称`[悬赏]巨人`，类别`主线前三章`。不替换原`gaintKiller`第七区刷怪任务。
- 复用同一悬赏阶段账目和生产流程：检查旧达成报告→必要住宿→任务内跳轮→打开悬赏页刷新→进入迷宫→目标战斗→返城→提交发光达成报告→按住宿间隔休息。悬赏没有领取操作；刷新展示弹窗仍是可选事件。
- 任务源`_TIME_LEAP`单独声明第三章`cursedwheel_impregnableFortress`的`Triumph`，编译成独立TimeLeap子步骤。该声明优先于外部`ACTIVE_TRIUMPH/ACTIVE_BEAUTIFUL_ORE`，不改外部配置，也不多执行一次默认跳轮。
- 起点与返城终点是第三章要塞，用`fortress_city_background`而非通用建筑证明城市；本任务不走王城旅行。郊外依次进入不落的要塞、第十区-要塞3F领主室。繁中沿已采集的纯名称素材与现有本地化解析，不重新收集或复用骷髅装饰。
- 第一个目标通过`mark_auto`快捷键导航，不插入完整地图坐标或AutoMap搜索。目标遭遇复用原Battle定义；成功战斗回执后可取消等待3000ms，再确认迷宫新帧推进任务点。导航停止/无路线本身不视作击杀。第二个目标通过`dungFlag`快捷返哈肯，遇怪、宝箱、加护及停止仍交原事件/续行链。
- 巨人与单目标蝎女共用三段业务结算及现有指定次数/无限循环入口。方案、角色和正式配置不自动复制或改写，沿当前有效战斗配置执行，可用任务覆盖单独配置。

## 定向证据

`test_native_author --giant-bounty`通过：正式目录注册、4种外部跳轮开关组合的任务优先级、繁中原生图编译、无需AutoMap、成功战斗后实际3秒等待且期间输入为零、要塞身份、三段账目和新开始/跳轮/原地续段回执。仅替换叶子观察的等待检查使用真实FlowExecutor与业务实现，不是实机运行。

同一检查覆盖默认非矿石蝎女编译。原ReturnFortress子调用缺`encounter/stopped`出口的共享缺口已补齐，仍交RecoverReturn处理正常遭遇/续行，未改蝎女导航和跳轮默认值。

原生构建与检查日志：`next/.local/giant-bounty-native-test-build.log`、`next/.local/giant-bounty-native-check.log`。

隔离candidate85真实服务18758已完成桌面/窄屏2项任务选择、CAS保存和刷新重载检查；巨人出现在主线前三章，指定次数/无限循环入口可选，原方案及外部矿石/凯旋值保持不变。浏览器禁止设备、开始任务等操作，只允许配置读写；日志为`next/.local/giant-bounty-ui-check-both.log`，截图为`next/web/test-results/giant-bounty-task.png`。最初检查的label定位及读有效任务配置的POST白名单写错，已修正为combobox角色和只允许`/profile/effective`的只读POST；错误日志保留，不将测试拦截伪报成生产故障。

最终candidate86包含检查脚本修正与事实文档更新；EXE、全部3个前端文件和任务目录与验收candidate85的SHA256一致，不拿另一套产品代码替代验收。

## 部署与边界

- 通过原生命周期管理器部署17654，原candidate84 PID4232退出；candidate86 PID28688，实例`9D619B41-6DC4-44EE-9552-8876BDA0E929`。正式目录API已返回主线前三章的`[悬赏]巨人`，服务Idle/run_id=0。
- 正式profile部署前后SHA256均为`08D1496E1B122B93F2F90D69AA055665FC23496B939D0C22311B8400B71642CD`，不切换用户选中任务、不改方案、VPN、循环配置。独立18758已关闭；用户旧网页未强制刷新。记录为`next/.local/giant-bounty-deploy.log`。
- 以上candidate86为首次部署记录。用户随后明确要求观看一次实跑；当前部署已更新为candidate91，见下节。尚未完成真实战斗、返城、报告和住宿的整轮验收，不宣称自动跑通。
- 未commit/push。整体回滚使用同一管理器将原data目录切回保留的candidate84；不并行启动服务、不覆盖用户profile。

## 一轮现场调查

- 正式配置选择`GiantBounty`并启用任务覆盖，任务方案为用户已保存的`悬赏巨人`；全局方案仍为`法术+地裂`，其他方案未删除、未重排。实测安卓Clash VPN为CONNECTED、tun0，保持原VPN。
- 首次正式启动失败`COMPILE_NODE_LIMIT`：角色动作图错误地展开全部保存方案。现只编译冻结配置可选的全局/任务点/已启用特殊敌人方案，保留4608节点上限和首个同名方案语义。当前真实配置巨人图2306节点；带真实profile只读参数的定向编译检查通过。
- 要塞启动门禁缺通用城市就绪配方，已接入`city_screen()`；具体城市身份仍由原背景确认。公会退出/刷新完成不再仅依赖郊外图标证明城市。
- 要塞郊外图标与背景一起比较仅0.754，前景遮罩0.962且框在真实图标中心。正式语义配方改用已有`bright_mask`方式、阈值0.94；首次错误地附加普通模板参数的候选被作者校验拒绝，未绕过校验，已修正。
- candidate90的正式run已完成第三章凯旋跳轮、悬赏刷新、不落要塞第十区进入、标记快捷导航，真实进入独眼巨人及蛇身随从战斗；未施放技能。旧ACTIVE模板受背景影响仅0.719，战斗漏识别使导航仍在等待。新增文字前景遮罩与独立倍速HUD联合证明；文字遮罩单独在城市也能0.965，不能单独使用。candidate91正式识别接口上同一战斗帧Hit、要塞城市负例NoHit。
- candidate90运行目录`next/.local/c11-flow-product/data/runs/679D4C2E-7580-450C-8A96-49FBCD4E0F27/1`保存完整事件、动作耗时、诊断、结果和内存账目。结尾为用户请求停止及`OBSERVATION_RECOVERY_EXHAUSTED:ADB_TIMEOUT`，不算Completed。关键战斗实帧另保留`next/.local/giant-evidence-active.jpg`，要塞负例`next/.local/giant-bounty-before.png`。
- 当前candidate91 PID37196，服务实例`A6857A4A-290C-4F84-B8D9-123107A038B6`，17654，正式启动准备失败`ADB_TIMEOUT`。实例2管理器仍报告PID30388已启动；窗口Responding=False，ADB get-state为device但简单shell echo不返回。该shell诊断已中断；其他实例未操作。现有恢复只自动重开确认已退出的实例，不能把“进程尚在但失去响应”冒充已退出。
- 用户明确授权重启实例2后，管理器shutdown返回成功但PID30388仍无响应；重新核对实例命令行后仅强制结束该PID，确认实例已退出，再前台launch。新实例PID24520，其他实例及全局ADB服务未操作。正式启动链记录EnsureVpn由false到true、StartApplication前台确认，再进入原未完成战斗。
- candidate91进入技能详情后，倍速HUD被遮暗使combat_active漏识别，触发60秒游戏重启；停下并保留目录`runs/837504C1-EA94-464A-8B9F-8E1596199F09/1`。candidate92补明细独立锚点后已推进角色，但三重霞明细框`[785,1127,46,80]`仍超出旧ROI，旧范围得分0.354、扩展范围0.994；在再次重启前停止，保留`runs/C6B86A7A-5BA7-433D-BCA5-EE3B17DB2B21/1`。
- 按用户最新要求，candidate93把详情/确定/关闭搜索统一为全宽下半屏并上扩至600的`[0,600,900,1000]`，保留战斗和当前角色组合条件。源公共流程及正式保存的三条公共流程6处关闭ROI同步更新；只做字段级CAS，原文件备份`next/.local/giant-roi-workflow-backup`，未覆盖用户其它逻辑。两张不同高度的真实技能帧Hit、要塞城市反例NoHit；原帧`giant-evidence-skill-detail.jpg`与`giant-evidence-low-detail.jpg`保留。
- candidate93 PID35716、实例`08D5AFDB-B785-4BBC-900D-88A5E45FB207`部署17654；单轮任务目录`runs/25616F7E-B786-4BBC-8190-0AE5C338EE3F/1`已真实释放三重霞并确认黄色倍速。随后出现死亡漩涡，Actor Entry未承接该场景，事件seq694执行`bound_game.restart`、seq695记录`CONTINUOUS_EXCEPTION_TIMEOUT`；用户要求先手动点漩涡进入拉人界面，停止请求未赶在关游戏之前生效，复活现场被打断，未实施手动点击。当前UserStopped、quiescent，Windows只读观察为安卓桌面，不自动重开游戏或续跑。
- 漩涡原帧保留`next/.local/giant-evidence-revival-vortex.jpg`，圆心算法及拉人界面未验。恢复段与完整一轮不能合并宣称成功；战斗结算、成功后的3秒等待、返哈肯、报告与住宿仍未完成。本轮未commit/push。

## 人工恢复复活现场

用户随后授权重启游戏并重新进入拉人界面。保持自动任务停止，通过实例2前台手动打开游戏、继续启动提示、处理0.01MB下载、解除Pause，再点击漩涡中心；窗口新截图确认进入手掌、黄色目标圈及白色动态外圈画面。预览capture提交后立即取得的PNG实际上是旧技能帧，已更名`next/.local/giant-preview-stale-skill-detail.png`，不能作为复活素材；等待异步capture完成后取得的新鲜原帧为`next/.local/revival-target-current.png`。旧`src/script.py`死亡处理是中心附近随机五次点击，不含圆心检测。

### 一次圆圈预测试点：失败

- 用户授权“试一下”后，使用本地诊断脚本`next/.local/revival_circle_probe.py`，未注册生产入口或部署。先保存约12秒只读观察，随后最多一次真实点击；期间正式任务保持停止，诊断IPC使用完后已断开并恢复正式设备连接。
- 黄色目标圈中心约(450,800)、半径105像素；通过可见黄色圆弧定位。白圈采样的多段估速约1302、1927、1974、2193像素/秒，存在明显不确定性，不能证明速度固定或变化。
- 临时输入使用ADB shell input tap，而不是生产常驻scrcpy通道。空input命令启动耗时中位数107毫秒被用作估计，真实tap命令总耗时127毫秒；两者都不是触屏事件实际送达时间。预测使用1950像素/秒及白圈目标半径26像素，这个时机假设尚无游戏判定证据。
- 实点(450,800)后出现134伤害，HP402/539降到268/539，仍停留拉人界面，明确失败。没有继续点击或重新启动任务。圆心定位不等于复活成功；正式死亡分派与复活算法仍未完成。
- 原始观察及前后图位于`next/.local/revival-live-observe/`、`next/.local/revival-live-click/`，命令摘要为`next/.local/revival-circle-click.log`。后续应先确认白圈周期/速度、判定时机和实际输入延迟，不能通过盲目连续试点消耗生命值。

### 用户授权追加试点：一次失败、一次成功

- 用户明确允许多次点击，并要求后续续关界面重新采图。本轮逐次确认画面，不自动续关、不重开任务、不消费宝石。
- 第二次总试点失败，HP268→134；证据`next/.local/revival-live-click-20261004-152512/`。第三次准备时低血量红色覆盖导致黄圈漏检，程序拒绝点击；原图`next/.local/revival-live-click-20261004-152617/before.png`保留。该拒绝不计为真实试点。
- 本地探针根据原帧扩大橙色目标及粉白外圈颜色范围，原帧重新定位到(448.5,799.5)、半径103.1、外圈312像素。缩短循环额外休眠至1毫秒，保存目录改用时间戳；预测改用当前最近三个观测点，目标白圈半径为黄圈的0.6倍。这些是探索性参数，未发布生产。
- 第三次总试点命中，出现复活光效并返回战斗指令页，原倒下角色恢复血量。实际输入(450,800)，ADB命令总耗时81毫秒；此次局部估速607像素/秒，与前两次差别明显，不能据一次成功认定匀速估计或输入时延已校准。
- 成功证据`next/.local/revival-live-click-20261004-152741/`包含before、click、0.03/0.1/0.15/0.5/1.5/3秒分段等待后的截图、observations.json及重新取得的新鲜900×1600原帧`battle-return.png`。截图文件名是各段追加等待时长，不是相对于点击的绝对时间。
- 当前正式任务仍UserStopped/quiescent，正式设备连接已恢复；游戏停在战斗指令界面，没有出现续关页，未为采图故意制造失败。累计真实试点3次、成功1次，不能记为稳定复活算法或整轮巨人任务通过。
