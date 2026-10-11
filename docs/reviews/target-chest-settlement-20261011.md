# 目标战斗结算后转宝箱误停

## 现场与根因

- 小时巡检确认原100轮批次 `giant-100-heal-fix-20261011-0700c9f9f1944e478320b65f7dbdf0d0` 已成功4轮，第5轮于04:23停止，原因为 `TARGET_ENCOUNTER_RESULT_UNCONFIRMED`。实例 `DC9A8D75-7AD8-4CE4-A3AE-B2B87CE6A854`、PID56924、EXE SHA256 `273df0882979982118d9547e0c26a72fbe84cc876e05c70b26170d6acdf7901b`。
- run5事件1597已用迷宫新帧确认 `dungeon_resumed`，同一场combat=1已结算、pending_combat=false。上层TargetResult选路后新帧转为宝箱，事件1608/1610进入TargetChestEnd/ConfirmTargetChestEnd；目标unit/point/combat、resume_authorized均有效，却因要求pending_combat=true而抛异常。
- 失败原帧确为繁中宝箱“打開／什麼都不做”，SHA256 `f7ffc0f2fc6dba6c975ec12043b681e2423d0eee4834a699948dee663827fbcb`；没有未决输入、没有救人现场。原result quiescent/details_complete/result_saved均true，secondary_errors为空。游戏仍在前台，安卓tun0为UP/LOWER_UP，不是游戏或VPN退出。
- 旧代码 `src/script.py` 的场景识别按实际迷宫／宝箱／战斗新帧分派，不要求返回后仍处于未结算战斗。本次保留原生遭遇身份及账目，不退回旧执行器。
- 原回执、事件、动作耗时、诊断及原帧已复制保存在 `next/.local/target-chest-settlement-20261011/before-*`，正式原记录不改。

## 修复范围

- 在既有WvdRunState中记录最后一次正常结清的combat序号，两条正常结算入口同步记录；再起撤销待结算战斗不产生该记录。
- 同一目标战斗已正常结清后，允许随后宝箱新帧确认其结果。没有未结战斗也没有同序号结算记录时仍拒绝；unit/point/combat、phase、恢复授权和死亡保护不变。
- 结算函数幂等，已结束战斗不重复统计，不把宝箱页面当奖励到账或宝箱完成。没有增加处理器、重新识别所有模板、扩大ROI、修改阈值或输入恢复期限。
- 日志business summary增加 `settled_combat_sequence`，用于区分“正常结算”与“复活撤销”；不改profile或保存流程。

## 验证与继续条件

- 在现有 `--target-chest-completion` 补入真实State/FlowExecutor的“先迷宫结算后转宝箱”和“先宝箱观察结算”的用例；保留直接宝箱、3秒战后等待、后续遭遇计数与无路线出口。追加再起和未观察战斗的拒绝对照，不把识别替身当实机证据。
- 相同新增夹具在旧生产实现失败：`TRANSITION_ASSERTION:TARGET_ENCOUNTER_RESULT_UNCONFIRMED:ConfirmTargetChestEnd0`，exit=1；记录 `before-regression.log`。
- 该现有合同入口接入recognition定向回归登记。第一次修复已完成47个app/recognition/runtime合同入口，`next/.local/tcs-261011/receipt.json` complete=true、无设备动作；正式构建和部署的产品输入 `5843782602987f34f5e5ce40e0095cfe1a709fc09b6c349ad37fb33bd066e2c3` 与回归一致。EXE `309f3c844307ef13a4967173765d4c34f49836fca3653d2d2be6f118731e22a5`，实例32DBAE68-394B-417F-94A1-0A57E8D90515/PID34236。
- 首次定向回归的转场新旧对照已通过，但Application备份事务用例因隔离根较长、元数据备份路径261字符而失败：`WORKFLOW_READ_FAILED:backup`。原失败保留在contracts.log及对应完整日志；缩短隔离根至 `next/.local/tcs-261011` 重跑同一套合同，未跳过用例或改弱断言。WorkflowRepository普通CreateFileW的长路径适配未在本故障范围修改，保留为独立问题。
- 原成功4轮作为续跑基数，只开启剩余96轮；失败尝试不计成功，旧人工停止批次的2轮不计入此目标。实际续跑身份和回执在通过部署后记录。
- 内存仍为 `RESOURCE_UNRESOLVED`；本次不新增系统采集、不提交或推送源码。

## 实际续跑暴露的返城入口缺口

- 首次续跑请求 `giant-96-chest-settlement-20261011-b2354fc85f93413db32e549f0eab8709` 已submitted/Running，实际完成现场宝箱打开、选人、拆锁和奖励继续，随后进入InspectReturnCity。不是只收到accepted。
- 返城子流程Entry只列Done/Harken/Dungeon/Back等候选，没有调用者已有的encounter出口。新战斗出现后，所有正常候选均NoHit，事件235记录等待61051ms后 `CONTINUOUS_EXCEPTION_TIMEOUT` 升级重启游戏；这是另一个实际业务入口缺口，不是模拟器崩溃，也不是需要修改60秒网络恢复策略。
- 原帧 `intermediate-return-combat.png` 为有效战斗，SHA256 `5752aa72533b08968b70b5b3a50ee2e28a3247eb70a14d3853236d019de88e33`；结果、事件、诊断保存在同一证据根的intermediate-return-*。本轮由代理主动停止修复，原回执为UserStopped/STOP_REQUESTED/quiescent，无pending；并非用户新下达人工停机指令，续跑授权仍有效。该尝试成功0轮，不改原基数4。
- 只补返城Entry到既有EncounterExit的只读交接，沿用RecoverReturn及现有战斗/宝箱/再起处理；两种返城目的地共用同一函数。宝箱判定复用既有chest_page_condition，覆盖开箱、选人、拆锁和奖励页；没有复制战斗处理器或新增点击。
- 同一target-chest-completion合同增加真实返城Call入口的战斗、宝箱、直接奖励和再起四种交接，要求先返回RecoverReturn且输入0。最终47个app/recognition/runtime合同入口通过，`next/.local/tcs2-261011/receipt.json` complete=true、device_actions=false；输入b1d4c70127cb56c9be280ba073c9ef911ea97a3b2cd1837535ac805b107863c3/1299项。
- 实际Service读取中间故障原帧并使用正式发布资源，只读验证clear/menu均Hit、ended/chest均NoHit，日志return-entry-real-frame.log，exit=0；识别能认出战斗，缺口在调用入口交接，不是需要扩大ROI或降低阈值。

## 最终部署与续跑

- 正式完整构建/冻结打包完成，DELIVERY_STATUS的产品输入与最终回归一致。最终EXE SHA256 `3991d52967056ccadb470aab7135898a3f5292b457ccf85f144ef830bc0b77e9`，实例 `0E0B77E8-7599-4886-84EF-2AD5C6415F71`、PID63136、process_start_filetime134361430235918261；正式端口17654与原data-root不变。
- 续跑请求 `giant-96-return-entry-20261011-6b5f52e236af420594589080391f1f5b` 已submitted/run1/Running，从Boot_Title推进到FinishBattleForInspection现场战斗，事件175倍速输入confirmed、pending移除，f82新截图为爱丽丝技能页；不是将accepted等同于运行。后续完整轮次和同条件转场继续按自然日志核对。
- 累计口径为原成功4轮加最终续跑completed_cycles，上限总成功100。中间0成功尝试、原失败run5、旧人工停止的2轮都不计数；可追溯账目 `next/.local/target-chest-settlement-20261011/resume-ledger.json`。
- 正式profile SHA及revision与原基线一致，保存流程未改；救人、多人死亡与Unknown保护保留。未提交/推送，没有新增系统内存采集。完整100轮、同口径自然宝箱恢复延迟及内存归因仍待证据，不因构建/定向回归通过而宣称闭合。
