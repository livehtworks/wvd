# C09 正式历史保留与归档承接审查

2026-10-26；依据[计划C09及第5节](consolidation-20261026.md)、[数据职责](../data-authority.md)、[详细数据职责](../../next/docs/data-authority.md)与当前SubmissionStore、RunStore、Application源码。C08并行修改中，因此下文优先引用函数名；这不是新版本部署或正式迁移完成证明。本次只新增本页（预算12KiB以内），不改源码、登记或正式数据，不启动游戏/监控，不更换服务。

**结论：requests和runs应继续原位作为唯一权威，暂不迁移完全可行，也是本轮最轻路径。C09整体仍OPEN。** 旧published完整树承载原身份、资源与历史复现，并非仅凭“大、旧、重复”即可移动的垃圾。C08离线通过不等于当前正式服务已能读取新布局；在原服务仍运行且没有部署/租约释放证明时，不得替换或搬走正式历史。已有trash授权不自动补足这些承接条件。

## 1 已有消费链与保留范围

| 对象/入口 | 真实行为与最轻承接 |
| --- | --- |
| `requests/<sha256(id)>.json`，SubmissionStore::replay/save/scan | 原请求intent、接受/失败/完成receipt的持久幂等权威。replay在文件不存在时返回nullopt；读到记录后核ID、intent、schema、receipt，原回执加accepted/replayed返回。scan对未知、pending、损坏、超限阻断写入。**整套原位保留**，不要以归档清空数量限额。 |
| Application::start_task/start_pipeline/start_workflow→queue_run | 各入口先replay再require_writable，queue_run再次replay并在启动准备worker前save preparing；后续沿原链替换接受/失败回执。不是“重发旧ID就重跑”。这些入口必须共同消费未来归档缺失/损坏判定，不能只修一个API。 |
| NativeRunCoordinator::request_snapshot/owns_request | 仅核当前内存request_id/snapshot，不是持久历史查询或归档索引。不能用其返回nullopt推断旧ID从未存在。 |
| RunStore::read_summary | 固定目录读取run.json/result.json；无result返回Interrupted/NO_COMMITTED_TERMINAL、resume_allowed=false，旧缺details_complete保持null。终态只认result，不能由events或归档索引重造Completed。 |
| Application::diagnostic_image→RunStore::read_summary/read_diagnostic | `/api/v1/diagnostics/<instance>/<run>/<generation>/<id>.png`：当前run读现有store，否则拼`data-root/runs/<instance>/<run>`；从终态索引核instance/run/generation/id/path/status和SHA，再读图片。移走runs立即破坏旧URL。当前无通用历史run列表GET/按旧request查询GET，不为本轮凭空增API。 |
| `/api/v1/runs/current`、current/events、stop | 当前视图/事件环/停止职责，不能代替全历史目录清单。run_directory还被采集脚本消费，活动run严禁移动。 |
| `published/<request>` | BundleLease/Service/OCR持固定资源；C07指定录帧CLI用历史程序身份与资源重放识别。run.json保存request_id/program_revision等，需能从request定位原发布内容；不从当前作者包重建旧资源。保留program/flow、identity、可达原资源与provenance，修改物理布局不能修改身份。 |
| 发布引用盘点 | `inventory_publications.ps1`从run.json的definition.request_id建立引用；这不是活动租约总表，也没覆盖全部准备、缓存、预览及未提交事务。C08新资源根分类与request→digest引用是必要条件。 |
| 外部只读工具 | `analyze_run_timing.py`直接读run.json/result.json；`analyze_memory_owners.py`读diagnostics/memory-lifecycle/recognition日志；`capture_idle_heap.ps1`、`collect_memory_stacks.ps1`依赖run_directory和当前身份。保留runs原位即可承接，不需要为这些工具新建档案服务。历史账本中的绝对结果/图片路径继续有效。 |

补充C08工具消费点：`tools/audit_locale_wiring.py`按published全部直接条目mtime选最新目录/program，当前会误选.asset-sets、asset-objects或事务目录。应只接受经过类型核验的请求目录，并保持读取真实程序；不能让归档标记或共享根被解释为“最新发布”。

必须在线保持：用户profile/workflows；全部requests幂等事实；全部runs终态、输入/事件、必要失败图及索引；所有活动/准备/预览/批次冻结源、未结输入、无终态/未知恢复关系、cleanup-pending/staging；当前与已核回滚候选及相应可读历史。此处“在线”表示仍由现有入口可定位，不授权把原永久失败图改为可丢弃采样。

## 2 旧published大树去trash的必要单位

最轻可实施单位是**单个明确请求的旧完整发布树转换**：请求根仍留原程序与身份，以C07新marker定位同盘共享集合；原完整树移入同盘trash仅作精确回滚材料。不是把整个published目录搬家，也不是让运行时在trash里猜路径。原requests/runs及其路径不动，故不需要同时开发请求/运行归档索引。

转换应复用现有Publisher/BundleLease的受信加载、OwnedBundleDirectory事务和C07身份规则；不增加外部自动清理守护器。输入是指定旧请求root与精确成员身份；输出是同request root下可验证的新布局及可恢复的旧树映射。执行流程至少满足：

1. 先核部署版本具备新/旧读取能力；明确原服务/准备线程/OCR/源缓存/预览/批次对象退出。busy=false或result_saved不充分；不能试图靠rename失败代替租约盘点。
2. 对单个旧发布包从原锁定字节恢复最终资源集合，正确应用source_files/image_sources/mod_files规则；核program revision及flow/identity/provenance保持。原包缺身份或来源无法验证时停在该包，不从当前作者源补造。
3. 建共享集合和替代请求staging，完整核SHA、逻辑并集、共享descriptor/marker、物理闭包、消费者加载。原大树在切换前仍是唯一有效请求输入。
4. 在动作前持久保存精确事务：request ID、原root与FileID/卷、原成员摘要/程序revision、新共享digest与成员、新请求staging身份、trash目标、恢复路径/条件、当前阶段。trash不得已占用、不得重解析、必须同卷；记录不能位于将移走且尚无定位入口的唯一目录里。
5. 有准备/切换/完成状态和失败关闭：旧root移入trash与新root生效之间可能存在空窗，必须由维护/准备门阻断访问与新准备。发生中断不得把root缺失解释为“原请求没发布过”。先恢复/完成本事务，再解除门；不得在运行中提供新旧自动fallback。
6. 切换后原request路径经同一生产读取链可读，历史录帧以原逻辑成员读取，run/request关联及原配置/结果SHA保持；回滚测试以精确映射恢复原root/身份且原新集合仍受其他引用保护。旧树移入trash没有释放空间，不能将逻辑重复量宣称为回收量。

转换后的trash材料仍包含正式历史原字节，是受保护回滚证据，**不是本轮可自动删除垃圾**。只有保留/回收合同及额外删除授权满足后才可谈永久删除。未知旧格式、孤立目录、来源不明者继续原位保护；按名字、时间或静态引用0不足以迁移。

当前阻断：没有已部署新读取支持及租约全面退出证据；没有逐请求转换清单/事务恢复验收；未确认所有历史旧格式均可恢复。C08的新发布复用验收不自动包含旧历史转换。这些阻断不妨碍C08离线工作或其他已确认可重建垃圾的trash迁移。

## 3 若未来迁requests/runs，最低接口合同

本轮原位保留时**不实施**这部分，也不新造索引。若后续决定实际迁移，必须在同一切换中由原SubmissionStore/RunStore增加受信定位；索引只是原历史权威的定位与完整性清单，不成为第二套回执生产者，不双写业务事实。

SubmissionStore需要统一解析三态：`Found(original_record)`、`AbsentProven`、`Unknown/error`。replay仅在在线目录与归档完整性清单均可信、所有所属分片可读且明确无ID时返回nullopt；有ID就核原intent并返回原receipt，不重新执行preparing/unknown。任何索引丢失/损坏/不完整、应有归档载荷缺失/SHA不符、路径越界、重复冲突、未完事务，进入原read_only状态并抛读取/未知错误，不能返回nullopt。known旧记录可按原受限读取返回，但全局未知时新ID不能进入save(false)。

必须同时修改scan/status/require_writable/save，而非仅replay：当前save(false)只看在线文件是否存在、scan只数在线目录。若只迁载荷而不改这些门，重启后在线计数下降且旧ID文件缺失，会再次接受旧意图。归档前需要受保护且可验证的“已启用归档及完整性代次”事实；否则整个索引消失会被误判从未归档。标记和载荷共同缺失不能从缺失本身证明空历史，须保留可信恢复事实并阻断。失效归档不得靠新UUID重试绕过保护。

RunStore需要按原(instance,run)在统一受信定位器取得唯一物理根，再由原read_summary/read_diagnostic验证；Application::diagnostic_image不可自行在线失败后猜trash。迁前后原诊断URL及generation/id/SHA不变，损坏归档返回明确错误；不能把丢失终态降为“新run可执行”。只读命令行分析器要么接受统一解析出的明确归档路径，要么先按精确受控流程恢复原路径，必须有实际验收，不能仅报告“文件在trash还在”。

## 4 保留、预算与最小验收

已确定的规则足够支持**继续保护和超额拒绝**，不足以推出正式历史退出期限：requests仍为1MiB/条、256MiB总量、65,536条；批次16GiB硬限/14GiB续轮及独立卷2GiB余量按C07保持；recent-frames的240张/128MiB不适用于永久失败图。上述都不是全历史容量上限，不授权通过搬走requests重置限额。

仍需具体核定的未知：

- 哪些请求历史具有完整且已承接的最终资源与程序，哪些缺身份/未结输入/恢复关系；每项旧布局与新布局均可实际读取的证据。
- 保留的唯一共享集合、请求程序/身份、runs必要事实及事务/回滚材料的当前总量；逻辑、FileID内容量、实占、可回收量分别测量。旧盘点不能当迁移时仍无新增写入的证明。
- 哪些历史必须长期保留及其可恢复定位范围；没有已确认天数/数量退出规则时，原历史继续保留，不猜7天/30天。
- 当前及回滚候选能否读取转换后历史；当前服务未部署新读取支持时明确禁止迁移。正式服务更换不在本次垃圾授权中自动取得。
- 当在线+归档索引达到原请求限额时如何保持防重又停止新意图；本合同默认原限额与拒绝，不自动将归档行排除计数以扩容。

C09最小验收应使用独立可丢弃data-root，真实原SubmissionStore/RunStore与生产加载链：旧ID同intent重放回执、异intent冲突、pending不重执行；原result/诊断URL读取和SHA不变；索引缺失/损坏/载荷丢失/中断均拒绝新执行；活动租约禁止移动；逐阶段中断可精确恢复；发布转换前后全部逻辑成员与program revision相等；必要失败证据保持。若本轮只保持requests/runs原位，则无需为了“归档测试”迁它们，但也不能把尚未形成的长期退出合同写作完成。

本次仅定义可实施边界和阻断，不执行任何正式历史迁移；A2/A5/A6整体完成仍需相应工程与现场证据，不能以本页或同盘trash目录存在替代。
