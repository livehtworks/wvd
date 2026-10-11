# 当前动作优先：全链检查整改

## 范围与依据

用户要求暂停循环，检查整条链的重复识别，先处理当前步骤，失败后再按分支排查；验证实际耗时和完整流程后部署续跑。定时检查保持关闭，不自动commit/push。

旧实现参考为`src/script.py`的`StateDungeon/startAuto`：当前路线先找所需快捷按钮，找不到再展开及处理其它页面。逻辑与的短路求值指前项为假时不求值后项，参见[Microsoft C++逻辑与说明](https://learn.microsoft.com/en-us/cpp/cpp/logical-and-operator-amp-amp?view=msvc-170)。该语言规则不是游戏自动化的完整行业标准；下述场景顺序、截图复用规则均明确是本项目的实施选择。

## 已确认问题与源码修改

| 位置 | 原问题 | 当前源码候选 |
| --- | --- | --- |
| FlowExecutor | 识别超过100ms后，下一次执行丢弃仍有效的截图及命中结果 | 撤除时间作废；仍校验设备/连接/输入身份，输入、等待、重访选路点时换图 |
| 输入及业务确认的编译/执行 | 选路、步骤守卫、实际执行反复检查同帧同请求 | 完全相同的请求复用同帧结果；不同条件和真正输入门禁保留 |
| WvdVision的all | 前项已不成立仍计算后项，剧情入口不存在也展开其排除条件 | 按声明顺序遇NoHit停止；已执行项Error传播，成功时所有输入资格仍须成立 |
| 单字段业务观察 | 纯任务进度也经视觉接口取图 | 转入既有BusinessPredicate；异常诊断节点保留原视觉/诊断契约 |
| 迷宫导航 | 正常迷宫先比较战斗、宝箱、再起、城市 | 迷宫与地图/角色面板区分后直接进入既有导航；失败才检查其它页面 |
| 地下城恢复 | 角色/恢复面板已经成立，仍先查其它流程 | 正常面板或迷宫直接分类；不改恢复效果、原角色重试和未确认输入合同 |
| 旅店退出 | 正常返回排在城市、剧情等检查之后 | 已付款菜单优先返回；退出就是终点，原宝石拦截和付款记录保留 |
| 悬赏返城 | Entry把遭遇排在城市/哈肯/迷宫之前 | 当前返回页面优先；已确认的目的城市不再查询战斗/宝箱反证 |

通用any/not及其子树仍保留各分支的输入资格否决：这些组合可能把失败子项转成Hit，不能省略子项提高点击授权。正常all的NoHit直接结束，不再扫描无关后项。资源完整性仍在编译冻结/发布阶段验证。没有扩大ROI、降低阈值、提高次数上限、增加重复处理器或外部循环。

旅店声明继承单人/多人死亡处理，不代表付款前执行这些识别；它们在当前流程不能解释页面后由原异常入口处理。正常输入只保留网络、下载、暂停等可能覆盖当前按钮的提示检查。公会同名关闭按钮的战斗反证尚保留，但all短路后，关闭按钮本身未出现就不会执行该反证；不能未经真实页面验证删掉它，再引入误关救人/技能页风险。

## 验证记录

- af1/af2见`observation-age-rule-20261011.md`。af2的19个runtime入口通过，不是19轮游戏。
- af3暴露新增慢识别夹具复用了故意两次等待的业务替身；修正夹具为本用例声明的一次完成，不修改原等待用例。
- af4暴露自动转换纯业务观察与`unexpected_only`诊断节点冲突；诊断节点不参与该转换，未放宽原生图校验。
- af5通过前述目标结算回归，但共享下载权限测试的业务替身始终返回Done，四个金币流程组合失败；替身改用生产的business_condition，原拒绝/允许和输入次数断言不变。
- af6的新叶子短路断言读错返回JSON层级，改为读取实际evidence.conditions；af7该项通过。af7随后在真实Service的supply-plan出现0xC00000FD。独立复测曾通过，但af-stack3的CDB实栈再次捕获：main从0xa0ed8670到0xa0faf790占0xd7120字节，OCR经evaluate_uncached进入ONNX后栈溢出；不是仅凭错误码推断生产递归或历史内存增长。现拆开测试入口，让供给/OCR用例不承受其余作者用例的大函数栈；不增大栈、不删断言、不替换真实Service。异常码及检查方法来源：[Microsoft栈溢出调试说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/debugging-a-stack-overflow)。
- af8的app/devices/recognition/runtime共52个合同入口complete=true，产品输入386a8776a700d3c7a591f158fc3030dc4aee4de6ebe83d033de273955495f12d/1299。af-extra的实际恢复/迷宫原图、救人/技能详情负例、正式profile副本、保存流程副本整链均通过。后两类不是实际游戏执行，device_actions=false。真实迷宫原图分类约112ms，恢复面板约4.6ms，均OCR0；不是进本到导航端到端耗时。
- 完整原日志及失败回执保留。独立候选与正式部署EXE均为7438bc031679cb0e354aa59780c8a88e1122e9a552295c591d8caa80713f59a6；实机验收见下，不用离线通过代替实机。

## 现场与耗时基线

整改前原正式批次run20已UserStopped/quiescent，累计28/100（原9加本段19份Completed）。profile哈希仍为`3e873d08fb9135f558c3e3348b3bfffff3b73555c63a9176cc75cff274ccb87c`，正式profile/workflows共41文件在验收前后及部署后逐一核对未变。

原run14至18从`Task_FirstDungeon_Entry`到标记导航实际接受分别为7440.08、5817.85、6602.21、5857.64、7238.40ms，平均约6591ms。每份事件序号与步骤时间存于`next/.local/af-261011/baseline-timing.json`；这不是加载动画起点到可见界面的像素测量。

候选独立单轮实例CD353AB4-5595-44DD-8858-27207BD5FEE2/run1，同口径事件784→807为869.8551ms，点击(760,357)，原始事件与`af-live-261011/entry-timing.json`保存。请求giant-action-first-validation-20261011-85ff4567739645d082d4e24c028b6f24，独立data-root/profile及workflows副本，已Completed/quiescent/result_saved，耗时773.3698571秒、无重启；business.bounty_cycle.completed_cycles=1，inn_rest_completed=true，inn_payment_pending=false。实际结算、返城、交悬赏及住宿完成，result SHA256为d8a5af4ad6475338321a4b512ac2b31b74163b550fc60bb2227a126c96e4d67f。PDB哈希及完整接受回执见`next/.local/af-live-261011/acceptance.json`。

该轮没有宝箱步骤，不能宣称宝箱后同条件实机耗时已通过。第一次恢复打开等待11771ms（183次模板比较、8次OCR调用）后由既有遭遇出口交给新战斗，随后第二次恢复完成；这段仍需原帧性能复核，不把所有等待都当作已消除或都认定必要。付款确认后等待8244ms（OCR0），旅店返回按钮后809.65ms即离开；动作完整记录保留，不混称为100ms以内或零等待。

验收成功一次计入原目标，累计29/100；已停候选17655服务并通过原打包/服务管理入口部署到原17654及data-root。当前实例563E32FD-B980-4FDC-A473-48244D2F0761/PID62136，剩余71请求giant-remaining-action-first-20261011-e75b589e8353449e94fabf99d94ef20b已实际Running/run1，从公会推进且有新输入/截图。定时检查保持PAUSED，未commit/push。历史账目备份、接受回执和续跑请求位于`af-live-261011`；当前总数按29+本请求成功数计算，不重复计完成段。

正式续跑run1同口径事件508→531为725.0844ms，见`af-live-261011/formal-entry-timing.json`；之后已推进战斗事件1872，不仅是请求accepted。该轮尚未Completed，不计入上述29。

内存仍为RESOURCE_UNRESOLVED，不把本轮减少重复识别或编译问题当成历史内存增长归因。没有新增WPR/ETL/堆采集。
