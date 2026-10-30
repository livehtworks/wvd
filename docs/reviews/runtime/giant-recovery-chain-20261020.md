# 巨人悬赏整链及重启衔接审查

## 范围与证据边界

本轮检查实际 GiantBounty 工厂、正式保存公共流程的只读副本、Application 装配、Coordinator 的恢复许可、原 FlowExecutor 和 WvdRunState。未新建执行器或外部循环，不修改用户 profile/workflows，不自动 commit/push。

“保留路线记录”限定为同一根run内的游戏/模拟器生命周期恢复，沿用Coordinator持有的业务状态；不是新增跨WVD进程的状态恢复。WVD服务重新启动仍从实际现场检查旧报告/遭遇并进入新轮，半轮不算成功。此次部署等待完整轮次完成再停止，累计以磁盘Completed回执为准。

证据目录：`next/.local/chain-261020/`。`source-before/` 保存本轮修改前相关源码；`baseline/` 为修改前25处恢复检查。此夹具只隔离设备送达与图像叶子，流程工厂、公共调用、状态、确认事件和执行器使用生产实现。它不是设备崩溃实机验证，也不是游戏25轮。第一版宝箱输入没有声明 `chest.open.option` 资源叶子，两个宝箱 Boot 停点不能作为生产缺陷证据；后续夹具已补真实配方叶子。

旧代码对照：`src/script.py` 的 `RestartableSequenceExecution`、`StateDungeon` 和 `Scorpionesses`。旧实现按业务段重新进入，不要求所有流程扫描完再执行当前动作。当前实现仍须保留实际未决输入、已完成点和已消费技能的各自责任，不能直接删除这些保护来模仿旧 Python 的盲点。

## 逐段结果

| 段 | 正常出口与恢复入口 | 本轮结论 |
| --- | --- | --- |
| 启动/设备恢复 | Application 装配原 recovery_policy；Coordinator 核绑定设备、清理和输入账目后重入同一 unit | 原策略把可由本任务收尾的 pending 和跨任务不准转交的 pending 混用。候选仅对已证实设备故障、无未决输入的悬赏允许原所有者继续；未知付款、跨城跳跃、其它未确认副作用仍拦截 |
| 查旧报告/揭榜 | 原公共公会入口、列表退出、展示页关闭 | 已有公会/列表恢复可通；初始哈肯菜单与残留领取回执补接既有返城/归还子流程 |
| 跳轮 | PrepareLeap → 原 time_leap → Leaped | 保留“准备不等于提交”的重入语义，不重复准备；跨 unit 或跨城转移不放行 |
| 入副本 | enter_dungeon → traverse_dungeon | Boot 可认出再起/奖励/哈肯菜单，但入本入口未接；候选补同一遭遇/哈肯入口，不凭页面直接记任务完成 |
| 标记导航/目标战斗 | 现有 target attempt、combat receipt、目标点确认 | 保留上一轮 destination_finished 交接；不恢复迟到提示消失后再等60秒的错误路径 |
| 已完成点后重启 | 原确认事件 → 同 unit 重入 | 原确认ID包含重启序号，导致重新入本清零。候选只对同一悬赏路线保留确认ID；普通迷宫原重启策略不变，跨 unit 不共用ID |
| 宝箱/奖励/再起 | 既有 chest/revival 子流程 → 原路线分派 | 保留救人、多人死亡、Unknown保护；不另建关闭救人动作，不以复活取消记目标完成 |
| 回复面板 | 既有回复/BACK/左下X/遭遇交接 | 本轮不改两次BACK、等待、ROI或阈值；重启后已完成目标不得因重新入本再打一次 |
| 哈肯/返城 | 已确认两点 → 原 ReturnGuild；遇敌交 RecoverReturn | 入口补接哈肯菜单，但仍要求任务点账目；不能凭任意哈肯菜单跳过未完成目标 |
| 归还 | Report → 实际领取回执 → 关闭 → 退出公会 | 原实现关闭回执后才记账，重入又无条件停止 pending。候选先凭实际回执记账，再关闭；准备后仍见报告按钮可由原提交步骤处理，缺回执且按钮消失仍停止，不猜成功 |
| 住宿 | 已付款页面 → Paid → Leave → Completed | 修改前夹具实际出现提交数1→2：Boot先退出菜单导致丢失付款现场。候选把有悬赏住宿账目的菜单交回原住宿流程；启动层不确认付款 |
| 批次衔接 | 三个 unit 完成 → Application 保存 Completed → 下一轮 | 不改变成功计数和失败排除；部署后按已保存回执只续剩余数量 |
| 每段记账与返回之间 | 揭榜/返城/整轮完成事件已确认，但原 unit 尚未返回 | 补按原阶段及原 unit 的完成事实返回；不再拿下一阶段要求阻塞上一段收尾。没有对应确认或 unit 不同不能使用此出口；六手四段共用状态逻辑也检查对应边界 |

## 验证状态

candidate4 的34处恢复位置全部通过，`candidate4/recovery-audit.json` 为 passed=true、failures=[]。生产恢复策略、缺回执不猜成功、Unknown输入不重放、人工停止不恢复、普通迷宫策略保持、三段/四段完成边界等反例同时通过。每例从实际保存库副本和完整工厂开始，执行三段到完整终态；不是34场实机游戏。已付款恢复的提交数保持1→1，已完成目标不重复战斗，归还次数与可见回执一致。

可直接对照 `baseline/` 与 `candidate4/` 的同名证据：`audit-target-done` 和 `audit-heal` 的目标战斗进入次数从2降为1；`audit-rest-submitted-recovery.json` 的付款提交数原为1→2，新为1→1。`audit-rest-paid` 两版都是1→1，不能把这个已记账样本冒充旧版重复付款反例。三处新增段尾恢复没有独立旧可执行文件对照，结论限定为源码断链事实及新候选验证。

应用、识别、执行器三个受影响模块共53个验证入口全部通过，`next/.local/chain-contracts-261020/receipt.json` 为 complete=true、device_actions=false，使用独立可丢弃目录。正常完整工厂15场景/2转移保护反例通过，见 `normal-final/` 和 `chain-normal-linkage-20261019T182631622501.log`；最终构建后另核 confirmed-result、child-result-frame、unknown-input-manual-stop，均通过。不把不同层的验证数量相加当游戏成功轮数。

候选验证中保留的失败：candidate1 的测试进程发生栈溢出；CDB调用栈和反汇编确认 `run_author_cases` 固定栈帧 `0xD2628`（约842 KiB），叠加整链入口 `0x11430` 后在资源收集处耗尽1 MiB线程栈。整链CLI现直接从 main 分派，避开无关大测试函数；未增大生产栈或缩减检查。candidate2 随后报 `COMPILE_IMAGE_SCAN:Author_continue:invalid map<K, T> key`，新增启动回执条件使用的 `guild.report.receipt` 未在 `location_probes.hpp` 的既有资源表登记，已补登记。两次均为离线候选，未部署。

candidate3 被 `COMPILE_ORPHAN_NODE:Entry` 拒绝：反例测试直接把入口改成 CheckReports，使原 Entry 不可达。已撤销测试的入口替换，从真实原入口检查缺少归还证据时不猜成功、不重复点击。生产图校验不放宽。后续候选加入三个“已记账、未返回”的恢复点，共34个位置；通过情况以最终回执为准。

## 部署与续跑

- 原 build.py 的受控构建、package_functional 的输入/依赖/产物校验及打包、manage_service 的 Validate/Deploy 已完成，证据 `finalize.log`、`candidate-proof.json`、`validate.log`、`deploy.log`。
- 新冻结候选 `next/.local/chain-candidate-261020`；EXE `1e92ed7032ada6c02441711e3719cedb838714f2e42890e33cd06599fb56fdcb`，产品输入 `b903948e668d5d5c78ed7265a8443aded1378f1cb7f39e94fd1705c4170a06b8`，1317项，与模块回归输入一致。
- 原1F4FD481实例在run13完整完成后停止，磁盘13份Completed均核三业务单元/result_saved/quiescent；加原1E600A3F/run1–5的5份，本目标累计18，剩余82。`completed-audit.json`保存每份原回执路径和哈希；旧失败/半轮及其它历史100轮不加入。
- 新正式实例 `181ACEA2-92B3-45CF-AB8B-41920F9DF5B1`，PID26652/start_filetime134369086378832275，17654和原data-root保持；请求 `giant-82-recovery-chain-20261020-87d54609b64c433eae648668f2a7dbe9` 续82轮。已核真实Running/run1，从公会、跳轮、揭榜进入战斗，新帧和技能确认事件持续推进；启动时本段0成功，不宣称100已通过。
- 27份正式profile/workflow哈希未变；没有重启游戏/模拟器的部署命令，未改驱动/渲染/VPN，未commit/push。原每10分钟监看已更新当前身份/账目，正常安静，人工及保护停止优先。启动确认及最新状态见 `resume-ledger.json`、`resume-confirmation.json`、`resumed-live.png`；这些仅对应记录时间，不代替后续实时现场。
- 后续实机首轮run1已保存Completed：三个Session各自Completed、generation1–3、三业务单元完成、quiescent=true、secondary_errors=[]；归还1次、目标点2、金币住宿提交1次/200G、付款未知=false、observation.recovery事件0。完整原结果及哈希/关键事件保存在 `first-completed-result.json`、`first-completed-proof.json`。累计19/100，run2已自动进入跳轮；计算基数仍18。本自然成功轮没有制造重启，不能冒充34种恢复位置都已在设备上复现。

## 保留问题

部署恢复准备：旧冻结候选 `next/.local/rd-candidate-261020` 保留，正式27份配置/流程按原哈希核对，不修改它们。若新候选必须撤回，只能先停止已核身份的当前运行并保存回执，再用原 `manage_service.ps1 -Action Deploy` 指向旧候选及同一17654/data-root；成功数量继续按保存账目核算，不回滚用户数据、不自动补跑失败轮。此处是人工恢复步骤，不是程序中的新旧双链或自动回退。

- MuMu `libRenderer.dll` 自动转储已证明宿主以 FAST_FAIL_FATAL_APP_EXIT 退出，但内部原因仍未归因；本轮流程修复不宣称修好了模拟器渲染器。
- 旧返哈肯无HUD展示页的停滞仍未确定内部原因，原图的既有后置/已知场景均无命中、无识别Error，证据见 `last-two-recoveries-20261019.md`。本轮只证明真正的哈肯菜单/遭遇/已完成路线可以接续，不把无HUD未知页猜成菜单，不凭单帧认定官方卡死。
- 历史内存归因仍为 `RESOURCE_UNRESOLVED`，不新增堆/WPR/ETL采集，不把趋势或宿主崩溃当作分配净增结论。
- 官方彻底无交互异常仍按用户约定由手机重置，不重新启用已撤销的反复探测/重启方案。
