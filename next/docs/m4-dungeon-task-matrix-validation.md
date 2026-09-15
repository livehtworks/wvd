# 43 个 Dungeon 实际 TaskID 离线矩阵

## 当前结论

2026-09-15：交付可执行夹具，**不是运行通过报告**。本轮没有构建、启动 native、连接设备、写用户配置、提交或推送；主代理持有串行测试租约。完整任务验收仍为 **0/43**，不修改 `migration/m4-task-status.json`。

- 43 个真实 ID，172 个独立 unittest 方法，无 `WorkflowTests` 继承，无其数百个方法的重复发现。
- 170 个场景已生成独立输入/图像描述，状态全部 `NOT_RUN`；`White-G-v2` 的成功和恢复场景显式 `BLOCKED`，通过断言失败报告，不使用 skip/expectedFailure。它的拒绝与停止场景仍可执行。
- `DH-4f`、`DH-Church-upper`、`DH-Church-auto` 的**正常楼层** trace 可执行，但完整楼层恢复矩阵因真实生产缺口阻断，见后文。
- 本轮只检查了 Python 加载、方法发现、固定 Git blob、资源名、显式宝箱落点及 trace 结构。不得把这些静态检查计入 Maa、开箱、任务成功数。

## 固定权威

| 输入 | 固定值 |
| --- | --- |
| Git | `6585f4075f5714ab522aa582993860c09af912c1` |
| `resources/quest/quest.json` blob | `c5398eff233aa6105c95cd7e4c7ca51fd4024945` |
| `src/script.py` blob | `0f80961a63d06f26ce7d76e33677dc9cfb9bdf72` |
| 任务分母 | 固定 JSON 中 `_TYPE=dungeon` 的 43 个 ID，包含原始大小写/拼写 |

每次准备/执行从 Git 读取字节并核验 blob，不读当前工作树的 quest 替代基线，不 import 旧 script。原始 JSON **完整复制**到独立 `.local` 目录供 `WvdQuestCatalog` 读取。原 script 也保存作证据，绝不执行。

独立期望依据旧源码：`TargetInfo` 140 起、`CheckIf_FocusCursor` 1195 起、`CheckIf_ReachPosition` 1225 起、`CheckIf_throughStair` 1239 起、`CheckIf_harkenStair` 1264 起、`StateEoT` 2472 起、`StateMap_FindSwipeClick` 2862 起、`StateMapSearch` 2941 起、`StateChest` 2996 起、`StateDungeon` 3108 起、`DungeonFarm` 3414 起。`DungeonCompletionCounter` 只在下次 EOT 结算已遇到的战斗/宝箱。

不读取生成的 Pipeline nodes 推导输入、不调用新编译器生产 oracle。`task_plan.source` 只与旧完整 JSON 对照，不能替代后面的真实执行断言。

## 执行方法

在仓库根目录，先独立生成可审查的完整期望，**不启动 native**：

```powershell
.venv-build/Scripts/python.exe -B next/tests/m4/test_task_matrix.py --prepare
```

仅在主代理完成构建、核对 EXE 身份并取得串行租约后执行单项。例如：

```powershell
.venv-build/Scripts/python.exe -B next/tests/m4/test_task_matrix.py DungeonTaskMatrixTests.test_Dist__success -v
.venv-build/Scripts/python.exe -B next/tests/m4/test_task_matrix.py DungeonTaskMatrixTests.test_DH_4f__recovery -v
.venv-build/Scripts/python.exe -B -m unittest discover -s next/tests/m4 -p test_task_matrix.py -v
```

方法名规则：`test_<TaskID 中连字符改下划线>__success/failure/stop/recovery`；ID 本身不修改。全组包含两个显式 BLOCKED 失败，**不能为了绿色全组而过滤它们后称全通过**。主代理应将长日志重定向到本轮独立 `.local` 日志，串行运行，保留确定失败，不循环重试挑 PASS。

`-B` 禁止在测试目录生成 pycache。运行时只在新建 `next/.local/m4-task-matrix-*` 中写入原始基线副本、合成图、bundle/profile 副本、输入、native.log、execution.json、RunStore 和逐场景期望/结论。程序打印证据目录。依赖现有固定 SDK/OpenCV，不安装新依赖，不读取设备配置。

## 真实执行边界

| 场景 | 完整输入/期望 |
| --- | --- |
| S / success | 城内起步，真实 `_preEOTcheck`/EOT/可见目标入本，逐项路线，实际战斗/开箱，退出或旧有限目标段完成；`Completed`、一业务单元、根终点及落盘一致 |
| F / failure | 完整入本前缀 + 首个地下城输入 `reject=true`；该次 backend 实际被调用，帧不前进；`Failed/CUSTOM_ACTION_FAILED`，零完成单元/宝箱/战斗/任务点，无重启 |
| T / stop | 同一前缀 + 首个地下城输入，`stop_after_calls` 发正式停止；后置未知图阻止后续输入；`UserStopped`、真静止、零完成单元/宝箱/战斗/任务点 |
| R / recovery | 七张 Pause 状态、六次真实无效恢复输入，`pause.physics_frozen`；正式 EnsureVpn/StopApplication/StartApplication 后才进入成功链起点；第二代次完整执行 S，crashes=1，第一代次无战斗/开箱结算 |

模板默认 40x24，24-bit 随机确定图案，Maa/OpenCV 真匹配。`harken2`/`Bharken2` 使用 80x80 和独立 15x15 中心变化证明抵达，不篡改识别返回值。每张图保持到对应 expected 输入；被动截图次数不增加 frame cursor。运动停止由同一稳定图的实际观察判定，**没有**截图计数推进。恢复的第七帧跳转仅由已确认 StartApplication 产生，并核对准确 lifecycle 调用序列；本组无 time_events。

地图规则：`position`/命名楼梯先拖动、选点、AutoMove，再经真实移动/战斗和重新开地图后证实；普通宝箱每个目标先发现并实际打开一个箱子，之后才穷尽该目标的全部旧搜索方向；`chest_auto` 先开启一个实际箱子，再第二次点击产生 `NoChestCanBeFound`。**不把只有空宝箱提示的路线当开箱通过**。正常哈肯/quit/最后出口坐标用真实外部后置图退出，不把发送移动等同目标抵达；最后出口不增加 `task_step`。`harken2/Bharken2` 是有限段地图终点，不冒称回城。

每个非末尾 `position` 插入一场单角色 Manual 防御战斗；地图重新确认才推进任务点。测试不假定刷怪名称能证明特定怪物被击败。非实际开箱的任务不伪造箱数。第一轮城内出发不伪造上一轮结算，因此 S/R 的 `dungeons=0`，并非 `completed_business_units=1` 就把该字段改成 1。

共同 profile：Manual 防御、WHO_WILL_OPEN_IT=1、普通拆陷阱、不开自动住宿/重组、不做初始/战后/箱后治疗、不启用穿墙。均是现有配置，不加测试专用生产分支。

## 逐项矩阵

下表计数是**预期 backend 输入数，不是实测结果**。`S/F/T/R` 中 F=T；B 表示该正向 trace 尚未具备。`P(x,y)` 为原坐标；`C` 为实际普通箱 + 搜索耗尽；`A` 为实际自动箱 + 第二次搜索耗尽；`H/BH/H2/BH2` 保留对应 harken 名称；箭头保留源任务点顺序。所有方向、完整 ROI 含排除区、嵌套 EOT fallback、RTT、逐次坐标/Swipe duration 在 `--prepare` 的 `<TaskID>.json` 中逐项完整保留，不由表格缩写替代。

| TaskID | S/F/T/R | 原路线顺序与该项限制 |
| --- | --- | --- |
| Dist | 19/6/6/25 | C→H；默认六视图搜索 |
| lounge | 25/6/6/31 | stair_lounge(292,394)→A→stair_shiphold(292,394)→H；两个真实命名楼层 |
| fortress-B1F | 17/6/6/23 | 左下 C(0,305,900,965)→左下 H |
| fortress-B8F_entrance | 31/6/6/37 | stair_fortress1f(720,395)→P(453,865)→P(500,918)→stair_fortressGate(720,1027)→P(720,1241) |
| fortress-10F | 18/6/6/24 | P(560,757)→BH2；地图有限段终点，不是回城 |
| DH-4f | 27/6/6/33 | P(500,610)→P(445,290)→P(820,605)→H(stair_DH_R4)；错误楼层退路生产缺口 |
| DH-6f | 33/6/6/39 | stair_DH_R6→P(72,972)→右下 C→stair_DH_R5→H |
| DH-7f-right | 61/6/6/67 | P(770,600)→P(770,1190)→P(500,1025)→右下 C→P(450,545)→P(610,1090)→P(450,925)→右上 C→H |
| DH-7f-auto | 14/6/6/20 | A→右上 H |
| DH-10f-test | 87/6/6/93 | stair_DH_R10→五个 P→两个左下 C→P(130,865)→左下 C→右下 C→stair_DH_R9→H；四个独立箱/排除区 |
| DH-10f-JR | 31/6/6/37 | stair_DH_R10→P(655,330)→P(500,385)→stair_DH_R9→H |
| DH-Church-upper | 31/6/6/37 | 左上 C→右上 C→P(816,710)→BH(stair_DH_Church)；错误楼层退路生产缺口 |
| DH-Church-auto | 20/6/6/26 | A→P(816,710)→BH(stair_DH_Church)；错误楼层退路生产缺口 |
| White-G-v2 | B/6/6/B | 原 `Mark_auto` 单目标，默认六方向；标记/中心变化完整独立 trace 未实现，绝不改成小写自动路线 |
| FFXI-2F | 15/7/7/21 | A→P(496,447)；EVENT/ZONE2/precheck；RTT=EVENT_VNH |
| FFXI-2F-elite | 28/7/7/34 | P(300,280)→P(670,920)→P(865,917)→P(496,447)；RTT=EVENT_VNH |
| FFXI-5F-4Elite | 33/6/6/39 | P(30,652)→P(80,1130)→P(820,1130)→P(870,652)→P(450,392) |
| FFXI-5F-2Elite-mid | 21/6/6/27 | P(30,652)→P(870,652)→P(450,392) |
| FFXI-5F-2Elite-bottom | 21/6/6/27 | P(80,1130)→P(820,1130)→P(450,392) |
| FFXI-5F-2Elite-left | 21/6/6/27 | P(30,652)→P(80,1130)→P(450,392) |
| FFXI-5F-Elite | 15/6/6/21 | P(80,1130)→P(450,392) |
| HSR-1F | 14/6/6/20 | A→P(720,1135)；原 AGMM1F 名称不修正 |
| IWO | 14/6/6/20 | A→P(186,1028)；IWOB1F；港口 RTT |
| IWO_2 | 14/6/6/20 | A→P(500,815)；IWOB2F；港口 RTT |
| malice-B3F | 16/6/6/22 | P(715,290)→H2；地图终点，malice_B3F 图片别名 |
| fordraig-B3F | 14/6/6/20 | A→H；保留五条自定义搜索 swipe，正例首视图找到，不声称其余失败重试已覆盖 |
| SSC_chest | 12/4/4/18 | A→SSC/SSC_quit，default ROI；世界地图直接入本 |
| LBC | 17/4/4/23 | C→LBC/LBC_quit；默认六视图 |
| AWD | 14/6/6/20 | A→P(766,974)；AWD2F |
| AWD-noGorgon | 20/6/6/26 | A→P(766,930)→H；AWD1F |
| GHB | 69/4/4/75 | 五个 P→左下/左上/右上/右下各一 C→GHB/GHB_quit，default ROI |
| COS_chest | 51/6/6/57 | 左上 C→右上 C→stair_3(720,822)→左上 C→左下 C→stair_2(827,547)→H |
| COS_chest_short | 14/6/6/20 | A→左下 H；COSB2F |
| DOE | 22/6/6/28 | P(713,1027)→右下 C→DOE_quit；矿物种类/收益未合成 |
| DOF | 29/6/6/35 | P(347,866)→P(400,1183)→左下 C(含专属排除区)→P(133,1241) |
| DOF_Plus | 14/6/6/20 | A→P(719,713) |
| DOW | 17/6/6/23 | 自定义 swipe C(含排除区)→DOW_quit |
| DOW_Plus | 14/6/6/20 | A→P(34,822) |
| DOL | 29/6/6/35 | 右下 C→P(552,599)→P(662,546)→P(819,333) |
| DOL_PLUS | 14/6/6/20 | A→P(34,1078)；ID 大写 PLUS 保留 |
| DOS | 28/6/6/34 | P(455,875)→P(185,1085)→左上 C→DOS_quit |
| LMG-GT | 27/6/6/33 | P(393,545)→P(657,755)→P(555,1235)→P(178,1240)；LMG1F |
| LMG-FT | 27/6/6/33 | 同四坐标，独立 ID/FTB1F 入本定义，不能复用 GT 的执行记录 |

## 每项资源

以下为每项传给 helper 的 `extra_images`，即使与 helper 基础资源重复也保留，helper 去重。全部与固定 Git 图片目录核对；不生成未知名字、不扫描出一个“能匹配”的动态 ROI。所有任务 `_FloorCheck` 均不存在，native **不传 floor 参数**；楼层语义仅使用上表命名楼梯和哈肯 stair 引用。

| TaskID | extra_images |
| --- | --- |
| Dist | Dist, EdgeOfTown, TradeWaterway, chest, harken |
| lounge | EdgeOfTown, TradeWaterway, chest_auto, harken, shiphold, stair_lounge, stair_shiphold |
| fortress-B1F | EdgeOfTown, chest, fortressb1f, harken, impregnableFortress |
| fortress-B8F_entrance | EdgeOfTown, fortressEntrance, impregnableFortress, stair_fortress1f, stair_fortressGate |
| fortress-10F | Bharken2, EdgeOfTown, fortressb10f, impregnableFortress |
| DH-4f | DH, DH-R4, EdgeOfTown, harken, stair_DH_R4 |
| DH-6f | DH, DH-R6, EdgeOfTown, chest, harken, stair_DH_R5, stair_DH_R6 |
| DH-7f-right | DH, DH-R7, EdgeOfTown, chest, harken |
| DH-7f-auto | DH, DH-R7, EdgeOfTown, chest_auto, harken |
| DH-10f-test | DH, DH-R9, EdgeOfTown, chest, harken, stair_DH_R10, stair_DH_R9 |
| DH-10f-JR | DH, DH-R9, EdgeOfTown, harken, stair_DH_R10, stair_DH_R9 |
| DH-Church-upper | Bharken, DH, DH-Church, EdgeOfTown, chest, stair_DH_Church |
| DH-Church-auto | Bharken, DH, DH-Church, EdgeOfTown, chest_auto, stair_DH_Church |
| White-G-v2 | DH, DH-Church, EdgeOfTown, Mark_auto |
| FFXI-2F | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/GCN, FFXI/ZONE2, chest_auto |
| FFXI-2F-elite | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/GCN, FFXI/ZONE2 |
| FFXI-5F-4Elite | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/zone5 |
| FFXI-5F-2Elite-mid | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/zone5 |
| FFXI-5F-2Elite-bottom | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/zone5 |
| FFXI-5F-2Elite-left | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/zone5 |
| FFXI-5F-Elite | EVENT, FFXI/EVENT_GCN, FFXI/EVENT_VNH, FFXI/zone5 |
| HSR-1F | HSR/AGMM1F, HSR/HSR, chest_auto |
| IWO | City_portTownGrandLegion, IWO/IWO, IWO/IWOB1F, chest_auto |
| IWO_2 | City_portTownGrandLegion, IWO/IWO, IWO/IWOB2F, chest_auto |
| malice-B3F | EdgeOfTown, harken2, malice_B3F, malice_malice |
| fordraig-B3F | chest_auto, fordraig/B3F, fordraig/labyrinthOfFordraig, harken |
| SSC_chest | SSC/SSC, SSC/SSC_quit, chest_auto |
| LBC | LBC/LBC, LBC/LBC_quit, chest |
| AWD | AWD/AWD, AWD/AWD2F, chest_auto |
| AWD-noGorgon | AWD/AWD, AWD/AWD1F, chest_auto, harken |
| GHB | GHB/GHB, GHB/GHB_quit, chest |
| COS_chest | COS/COS, COS/COSB2F, EdgeOfTown, chest, harken, stair_2, stair_3 |
| COS_chest_short | COS/COS, COS/COSB2F, EdgeOfTown, chest_auto, harken |
| DOE | DOE, DOEB1F, DOE_quit, EdgeOfTown, chest |
| DOF | DOF, DOFB1F, EdgeOfTown, chest |
| DOF_Plus | DOF, DOFB1F, EdgeOfTown, chest_auto |
| DOW | DOW, DOWB1F, DOW_quit, EdgeOfTown, chest |
| DOW_Plus | DOW, DOWB1F, EdgeOfTown, chest_auto |
| DOL | DOL, DOLB1F, EdgeOfTown, chest |
| DOL_PLUS | DOL, DOLB1F, EdgeOfTown, chest_auto |
| DOS | DOS, DOSB1F, DOS_quit, EdgeOfTown, chest |
| LMG-GT | LMG/LMG, LMG/LMG1F |
| LMG-FT | LMG/FTB1F, LMG/LMG |

每项都传相同封存 alias 表：`returnText.png→ReturnText.png`、`returntoTown.png→returntotown.png`、`Mark_auto.png→mark_auto.png`、`malice_B3F.png→malice_b3f.png`。前两条避免 Windows 大小写文件冲突，第三条仅 White 使用，第四条仅 malice 使用；不改变任务目标的大小写语义。每项 JSON 显式保存该表。两个大模板只使用固定目录中已有 harken2/Bharken2，非新名称。

## 未承接与限制

**真实生产缺口，未改弱用例：**固定旧 `CheckIf_harkenStair` 在目标 stair 不存在时调用 `StateMap_FindSwipeClick(TargetInfo('harken', None, None))`，没找到再搜索 Bharken。当前 `native/games/wvd/vision/recognizers.cpp` 的 `harken_stair` 分支只返回 `wrong_stair=true/NoHit`；`navigation/map_route.cpp` 没有调度该两阶段全图搜索。影响 DH-4f、DH-Church-upper、DH-Church-auto。正常楼层正例不会验证这个退路，主代理必须单独承接并补因果错误楼层场景后再收口。

**明确夹具阻断：**White-G-v2 保留原 `Mark_auto`；旧 `startAuto` 只特殊处理小写 `mark_auto`。本文件未完成该大小写/普通地图搜索/默认六方向/中心变化组合的独立正向 trace，S/R 用 `FixtureBlocked` 失败，F/T 继续用真实全定义。已有单个 mark_auto 子图测试不能抵消此项。

**每项共同未覆盖：**连续周期、开启住宿/组队/治疗的 profile 矩阵、mod 来源矩阵、途中战斗或已付款后的恢复对账、错误楼层、所有 EOT fallback 重试、未知后置与冻结升级、实图质量和真设备。S 是有限 `iteration`，不是无限 DungeonFarm。R 从 Pause 故障后城内重启，不是原路线中途恢复、不证明已开宝箱不重复。F/T 不代表每个付款/开箱/战斗内部停止点。

EOT 正例对第一步城镇嵌套 fallback 和 EVENT 顺序发真实输入；目标直接可见时按旧规则不执行其他 fallback。intoWorldMap 的目标直接可见，未覆盖源 swipe/dismiss 候选失败重试；定义与输入 JSON 仍保留这些参数，不覆盖掉它们。`_RTT` 仅由真实 catalog 带入并核对，当前城内起步且补给关闭的场景不执行回城补给；FFXI/IWO 等不据此宣布 RTT 通过。普通 harken 的首视图命中也不证明全部其他视图重试。

合成测试仅验证限定业务条件，不验证指定怪物身份、真实掉落、矿石品类、收益、用户放置标记或语言/缩放/遮挡泛化。成功终点和完整旧任务能力必须分开登记。

## Native 接口与证据

主代理已接好但本轮未构建的契约：`workflow="iteration"`、`catalog_task_id=<原ID>`、`quest_catalog=<隔离完整固定JSON>`，经 WvdQuestCatalog 加载全定义；输出 `task_plan` 和 `kind`。禁止 `route_targets/entry_steps/floor/return_destination/route_type` 覆盖，本文件不传这些参数，也不传 `pre_entry`。不依赖 `entry` 或 `dungeon-route` 去替代完整 iteration。

其余使用已有参数：`frames/transitions/run_root/output/files/profile/aliases`、`normal_units=1`、`stop_after_calls`、`attach_recovery/restart_frame/restart_action`；`pause_frames` 由 Python helper 消费以绘制 Pause，实际恢复由原生生命周期产生，不需要新增测试专用生产参数。

每次执行保存完整 `<TaskID>--<mode>-expectation.json`：固定源、完整 frame 描述、每次输入和因果说明、exact expected、资源/alias/floor/RTT。helper 保存实际 `input.json`、图像、native.log、output.json、execution.json。矩阵断言 EXE 前后一致、SDK DLL 路径/hash、task_plan 全源一致、mismatch=false、完整 backend 数和最终 cursor、逐 Session 输入累计、业务计数、终态和真静止、唯一持久化 result 与 snapshot 一致，成功还校验 root_task_id/generation/depth/node。不能仅看 Completed 或 backend 数之一。

后端拒绝也经过 InputGate 准入，所以 `inputs.accepted` 包括最后一次真实调用；它不是 backend 成功数，拒绝由指定 transition、未前进 cursor、明确失败原因及零业务完成共同证明。结果事件为有界窗口，不能要求其保留全部输入；完整顺序由 native 每次精确比对 kind/x/y/x2/y2/key/duration 和最终 cursor 证明。

本轮静态证据：`next/.local/m4-task-matrix-prepare-dcrik2kn/index.json`，43 项、170 NOT_RUN、2 BLOCKED、`native_executed=false/accepted_tasks=0`。方法发现核对 172 个唯一方法且直接继承 unittest.TestCase；逐项文档/参数/边界审查核对 2,876 次预期输入，无 override/stay。更早准备轮 `m4-task-matrix-prepare-txg466c0` 是修正前 trace，保留但不作为当前期望。主代理运行后新增本轮实测结论和证据路径，不能覆写历史失败为 PASS。
