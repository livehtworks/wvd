# 项目当前事实

## 本轮收束入口与状态

- 唯一执行计划：[维护阶段收束计划](work-packages/consolidation-20261026.md)；可变任务状态及证据入口：[任务登记](work-packages/consolidation-20261026-tasks.json)。原工程收束已完成；2026-10-27用户禁止线上CI，重新处理正式历史与保留材料两项，磁盘回收及历史游戏故障暂缓。[后续报告](reviews/consolidation-followup-20261027.md)记录实际进度；C09/T0不能以源码或同盘搬移代替关闭。
- 本轮审阅固定提交为 `179cfd851877cba2f2df401d86a905ff9ae088f6`，该提交仅包含6份报告、摘要及当前文档。盘点原始基线仍是 `205692c7bdea73634e780f1af0c70eef4fa62d0c`，不把后续文档提交写回历史基线。
- 下列本地数量、候选及服务身份截至2026-10-26盘点：主扫描19:32–19:45、模型补核19:55、运行状态核对20:04，均为Asia/Shanghai；不是本次文档整理重新实测的现场。后续有副作用操作前重核相关身份，不复用历史PID操作。
- 该快照中当前批次为39/200，run40为UserStopped且仅1业务单元，不计成功；循环停止、监控PAUSED。另一个工作区17个未提交路径、正式41份配置及原批次账本仍须保护，具体证据见盘点报告与任务登记。
- `next/dist`在2026-10-26盘点有105个previous、1个staging，其中71个缺有效当前标记头；下文97份/62份为2026-10-25发布时的历史口径。两组时间不同，不将旧数值继续用作当前清理清单。
- 本轮已按精确清单迁移1,152项、871,860文件、约96.80GiB逻辑量到D/S盘各自根trash；未永久删除，同盘移动释放空间0。六套本轮新增大夹具也已迁出，只保留必要小证据。完整结果/恢复顺序见[执行报告](reviews/consolidation-execution-20261026.md)，本地journal与每盘RESTORE-MANIFEST.json可逐项恢复。
- C02文档选择器、C03的24处素材来源修正、C04原触发规则及C05成功夹具收尾已本地定向验证；用户现已退役线上CI，Actions总开关和两工作流均禁用。C08限定离线验收及原build/package通过。C09 ordinary/repeat维护入口、小隔离真实中断/恢复和实际CLI转换/回滚通过；最新完整build/default package619成员通过，产品输入849f719b…afa8ce/1344，新EXEea388844…与PDB已配对，原Validate通过。正式未切换。
- [首次原生CI失败](https://github.com/livehtworks/wvd/actions/runs/40354209693)保留为历史：英语词表固定哈希失败及依赖步骤未传播前错；没有修改锁值或宣称修绿。用户明确不再做线上CI，这两项从当前交付条件退役，验证继续本地完成。
- 正式候选仍为`.local/t0-candidate-261025`，EXE284e860b…及其PDB（`consolidation-261026/protected-release/`）保留；默认`next/dist/wvd-next-native`现为本轮新候选。正式服务/41配置未变，旧17处修改三版本独立备份完整，39/200停止、监控PAUSED。C09正式历史兼容/退出未实施，requests/runs/published不移动，T0仍OPEN；不可自动续跑或恢复监控。
- 2026-10-26原轮工程整理已完成，代码/素材/工具/文档按目的提交并快进个人master、推送fork；当次用户选择止于工程收束。2026-10-27已按新要求进入下述第2/3项后续，正式服务仍须等具体切换方案准备妥当后处理，默认候选不等于正式已切换。
- 2026-10-27第3项功能承接已完成：旧17路径有效行为全部承接，无需cherry-pick；旧S工作区曾整体迁移并通过实际恢复往返，另50个旧测试/候选根也通过同盘迁移核验。终核发现这两个trash批次根已不在登记位置，原因及原件去向待用户确认，不再外推当前整树可恢复。17处修改三版本独立备份SHA仍全部通过，Git登记暂保留；当前默认与事务控制两根保留。详见后续报告，不将旧PASS改写为新事实。
- C09维护入口源码及小隔离验收已过：真实进程中断/显式恢复与回滚、缺件/篡改/低空间/占用拒绝；repeat按真实父请求+完整成功前缀核账，停止轮不计入，requests/runs不改。正式1334根仅stat得到控制文件合计48,011,096,376B，不能据此认为全部可转换。已准备仅第39成功轮一份、增量512MiB及另2GiB余量的具体维护方案，等待用户变更此前“正式服务保持不变”的边界；当前正式历史转换0，循环/监控不启动。

## 接手入口

- 先读本页及本轮计划/任务登记；2026-10-21的 [工作交接](handoff-20261021.md) 保留为历史入口，只有处理对应问题时才打开相关专项报告、日志或图片。
- 当前产品在 `next/`，C++20/CMake、Vue、OpenCV、ONNX Runtime。Maa及旧Python链不参与当前构建运行；历史源码和用户资产保留。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有批次，Coordinator拥有根运行及Session生命周期；没有旁路执行器或外部循环守护器。
- 正式 `profile.json`、`workflows/` 是配置及用户流程权威，见 [数据职责](data-authority.md)。[核心合同](../next/docs/core-contracts.md)及[执行注意项](execution-notes.md)按主题查阅。

## 当前操作边界

- 2026-10-26完成[维护阶段收束盘点](reviews/consolidation-inventory-20261026.md)，确认191万文件及292.644 GiB逻辑量，已单列[T0产物生命周期事故](reviews/t0-storage-lifecycle-20261026.md)：1,334份发布快照、大夹具与旧候选持续累积，整改未完成。当次仅将事故/盘点报告及相关当前文档commit/push到个人远端；后续执行结果见本页顶部，不将报告交付或工程迁移记作事故关闭。
- T0六包源码及离线验收已完成并以`66568fb28c6693f4c39bb3c6d89562fe7e39fb51`推送个人远端`fork/codex/t0-lifecycle-repair-20261025`。66原生入口及12工具/前端阶段证据见[修复验收及发布](reviews/t0-lifecycle-repair-20261025.md)。2026-10-25按用户新授权完成构建、打包及正式部署，`.vscode`排除并保持。
- 新的独立200成功轮次根为`giant-200-t0-20261025-b0ad41332a754a33b4d900f14f3b2424`，GiantBounty/zh-Hant，不计旧批次。2026-10-26收束盘点已核当前实例run1–39共39份Completed/三业务单元/result_saved/quiescent；run40为UserStopped、仅1单元且已保存/静止，不计成功。原内部repeat已停止、busy=false，监控PAUSED；不据停止状态推断操作者，不自动续跑。原账本保留于`next/.local/giant-200-t0-261025/`，本次40份回执路径/SHA及服务身份见`next/.local/consolidation-inventory-20261026/live-and-receipts.json`。
- 本聊天恢复根`giant-200-resume-20261025-9dfea98c3b21429d8018e359838bb7da`已核run2–71共70份完整成功；run72为UserStopped/STOP_REQUESTED、仅1单元，排除。后续手动根`75f3dc44-1896-4e66-a3ce-ba69dc7bab99`已核run73–94共22份完整成功；两段各自原始目标为200，按根请求分开记录，不自行合并。界面新失败请求的completed_cycles=0不代表旧回执丢失。
- T0原因为3221226085/STATUS_TOO_MANY_LINKS，非设备错误。正式新首轮已触达真实上限：检测模型共享对象及首别名同ID、1024链接，第二别名为独立ID复制，三者SHA均`8fe4bf6a…25fa9`，原正式历史不删改。实证见新账本`live-model-capacity-proof.json`。
- 上一目标已取消，历史成功仍为40/200：3E95E158/run73–83共11份 + E65BF7A4/run1–29共29份；run30为UserStopped/STOP_REQUESTED、1业务单元，启动前核result_saved/quiescent且pending为空。旧40、剩160及更早100目标均不计入新200。
- 当前诊断及分段回执审计见`next/.local/bundle-link-check-261025-065401-1b573c/diagnosis.json`，含92份完整成功的路径/SHA、run72排除项、三个模型的链接数及最新失败请求。原启动证据仍在`next/.local/giant-200-resume-261025/`；其首次DEVICE_START_OBSERVATION_UNCONFIRMED已通过原API重连同一实例2解决，与当前硬链接问题分开。旧回执/pending及profile/workflows保持，不清理正式published目录。
- 暂停前最后巡检为06:34：已记录64次`navigation.input_no_progress`，已保存窗口确认至少12次程序主动StopApplication→StartApplication，全部重启总数因事件resync缺口未知；不按失败会话数推算，未归因MuMu崩溃。问题尚未修复，最后巡检证据见`monitor/check-20261021T223447Z/inspection.json`，首次故障原图保留于`monitor/check-20261021T203440Z/`（仅归档未看图）。
- 2026-10-22人工暂停的状态、停止回执和静止确认保留于`next/.local/giant-200-new-261022/user-pause-20261021T230009Z/`；旧导航故障仍未根治。本轮新200来自2026-10-25的新授权，监控继续暂停。

## 源码与部署

- 当前正式候选为`next/.local/t0-candidate-261025/`，端口17654、原正式data-root不变；实例`9F2AF8E0-F5BB-4EBC-BAF1-2DA16D6D7653`，部署核PID47024、start_filetime134374039039026061。后续仍重核实时身份，不能按历史PID操作。
- 新EXE SHA256为`284e860b078f9e91599ac00f083b21d322359311c5118a1814ef9a0be1c3de34`；产品输入`04361624…41c132`、1336项，代码提交66568fb。BUILT回执和619成员交付清单、预检/部署及41份配置未变证据在`next/.local/t0-release-261025/`。
- 2026-10-25发布时默认dist旁97份旧历史中62份缺少有效新格式交付标记，原打包入口如实拒绝；当时全部保留。该次只读清点87,334项、14,825,767,070逻辑字节并另作保守空间准入，随后以同一原打包事务生成上述全新版本候选，没有修改旧标记或放宽校验。2026-10-26口径见本页顶部和盘点报告。
- 原`next/.local/rescue-marker-candidate-261020/`已通过回滚预检并保留；旧实例BAA009E9/PID22208经原管理接口确认退出。旧run94回执SHA保持，未覆盖profile/workflows。回滚方案及快照在发布证据目录，出现输入/数据未知时不得盲回滚。
- 已部署修复：友方选人双排锚点/缺框四边与唯一布局；补给等待交回迟到宝箱奖励；同owner同规则Boot恢复栈复用且保留父账；救人圆形X原帧外观补充，仍由原party_death门禁及中心动作处理。
- 旧候选历史识别/app离线39入口、Boot修复runtime/app离线34入口通过，均不是游戏轮数。本轮另有正式首轮发布及运行/实际输入证据；尚未完成200成功轮，不冒称全批或旧导航故障已根治。
- 上次提交前核对过当时1331项输入、旧测试二进制、三组夹具15用例/20处原帧引用及打包成员；本轮最终验收另有完整源码/二进制桥接与正式配置终核，不混用旧候选身份。
- 原build.py、package_functional.py、manage_service.ps1构建/打包/预检/部署分别有证据；发布时27份正式权威文件哈希保持。交接没有重新构建、测试或部署。

## 保持的能力与未解问题

- 既有路线迟到交接、EntryReady原目标续接、同unit入本/任务点回执、宝箱/奖励/再起/哈肯职责、归还凭回执先记账后关闭、住宿已付款不重复均保持；不得清业务pending或让下一unit借用旧回执。
- 单人救人保留已消费策略，只有真实再起确认才重置；多人死亡、低意志力、Unknown保护停止不得擅自抵消。保留现场不妨碍无副作用的隔离诊断，不得以重复静态巡检代替修复。
- MuMu内部libRenderer.dll崩溃原因及RESOURCE_UNRESOLVED仍未归因；已有dump不等于根因已解，0xc0000409不直接解释为栈溢出/OOM/取图致因。
- 不改驱动/模拟器版本/渲染/CPU内存/VPN，不上传日志转储，不恢复full dumpsys window；只使用可见MuMu实例2，禁止宝石购买、抽卡和卖装备。
- 保留debug/performance/recognition/memory及240张/128MiB最近截图策略；减少的是代理读取/展示，不关闭生产取证能力。验证必须独立可丢弃data-root，不能在正式目录写测试数据。

## 历史入口

- [交接前完整状态快照](reviews/project-status-before-handoff-20261021.md)：保留原文，包含大量过期身份和授权，只作历史证据，不作为当前操作许可。
- 专项报告及精确验证入口统一见[工作交接](handoff-20261021.md)，无需每轮全文通读。
