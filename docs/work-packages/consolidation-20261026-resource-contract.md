# C07 资源复用消费合同

2026-10-26只读审查；源码基线 `179cfd851877cba2f2df401d86a905ff9ae088f6`。本页是[收束计划](consolidation-20261026.md)第5节的实施限定，不是已实施状态。仅新增本页，预算低于20KiB；不写正式数据、不加载图片、不连接设备、不修改登记、不提交或部署。

**冻结时结论：C07接口合同可以冻结；没有发现必须改变既定请求root、程序revision或历史防重语义的冲突。C08在冻结时尚未实现、未验收，后续实施状态以[任务登记](consolidation-20261026-tasks.json)及最终身份回执为准。** 200目标存在资源层面的可达空间，但不能承诺任意诊断量下完成200轮。C08必须保留具体容量拒绝；C09历史查询/恢复/退出尚未完成，不能关闭A2/T0。本页列出的加载、计量和测试缺口都是C08必要修改，不是允许带缺口切换的例外。

## 1 身份与加载入口

旧发布器 `next/native/games/wvd/tasks/native_publisher.cpp:128–157` 是身份与覆盖规则权威：

1. 原identity包含engine_kind、program_schema、source_revision/source_files、mod_revision/mod_files、image_sources、native_semantic_source_sha256、aliases、authoring、definition和program。`identity.dump()`的UTF-8字节SHA256成为program.revision；identity.program保留编译时的 **`revision="pending"`**。最终flow是该program替换revision后的序列化结果。加载须核identity摘要等于请求revision，核identity.program复制后仅替换revision即等于flow；不能对最终flow直接重算原identity，也不能把新digest/marker加入identity。
2. 最终资源集合从全部source_files开始，仅对image_sources中`source="mod"`的path应用mod_files覆盖。未被选中的mod成员不发布；即使mod另有同名资源也不能覆盖。每个image_sources项的source只允许baseline/mod，path须存在于对应源清单，sha256须匹配；mod_files仅允许image/成员。aliases保留原含义，不能按basename猜选图。完整基础集合保留，不按实际语言删除未用成员。
3. resource_digest只覆盖schema=1和按path排序的files[{path,sha256,bytes}]，采用计划规定的紧凑、键排序、Unicode不转义UTF-8序列化。bytes来自固定句柄大小，不能信任未核声明。path经过原checked_relative、Windows大小写冲突与重解析保护。同hash不同path仍是两个逻辑成员。
4. `program/flow.json`、`program/identity.json`以及有mod时的`parameters/image-sources.json`在请求侧；后者内容等于原image_sources。无mod不新添provenance。来源本已占用生成路径的冲突仍拒绝，不能覆盖后冒充新身份。`publication-layout.json`是显式控制文件，不在逻辑资源集合或原identity内；存在但格式未知、损坏、digest/program_revision不符即失败，绝不退回旧布局。缺marker才按原完整树读取。

在原BundleLease/Publisher内实现**唯一受信加载入口**，普通构造和无lease的Service、Publisher源、历史回放都经此入口。其输入仍是请求root、revision、逻辑manifest（历史入口可从已验证identity/flow恢复）；不得接受调用方指定的任意共享根。只有严格digest可定位同一published下`.asset-sets/<digest>`。旧普通包仍可由原manifest严格加载；不能强迫所有作者包、模型对象都有program/identity。

共享侧锁集合根、manifest、payload祖先/目录/全部成员；请求侧锁root、flow、identity、marker及必要provenance。分别核物理闭包，再核逻辑并集与identity重建的最终集合完全相等。marker先锁后解析，防止检查与打开间被换；共享描述同样不能只哈希后释放。marker保留名不能解释为普通载荷：若现场旧完整包确有同名普通文件，这是具体格式冲突，停在该对象，不移走它或静默降级。本次当前目标源清单不含此文件；没有据此宣称所有历史都已检查。

Bundle.root/lease.root()保持请求根，Bundle.revision/lease.revision()保持program.revision；不能把共享根/digest塞进旧字段。组合lease持有两侧租约并映射逻辑成员；require_member/hash/member_size/bytes/copy_member/link_member/verify_members/file_count及新增物理成员解析都从已核底层句柄派发。copy/link不重新从逻辑root打开；physical_member只返回被当前lease固定的文件。identity()继续表示该租约实例，不能用digest导致不同请求缓存身份合并。hash_bytes、storage_stats仍表示本租约验证量和内存缓冲，不改成磁盘预算；控制元数据计量单列，file_count保持逻辑成员数。

## 2 消费点与改动边界

| 消费点（next/下） | 当前行为 | C08必须承接 |
| --- | --- | --- |
| native/platform/windows/bundle_lease.cpp:360–478、569–603 | 单root扫描全部成员，Held固定句柄；ONNX流式校验，其他bytes常驻 | 在同一实现内增加上述组合加载；原严格单根分支保留；新模式对两侧分别verify，不能只扫描请求树 |
| native/recognition/request.hpp:21–26 | Bundle持root/revision/files/lease | 字段语义保留，files为原逻辑并集；新控制元数据另存 |
| native/games/wvd/tasks/native_publisher.cpp:146、148、213–290及.hpp | baseline/mod重建单根lease；逐请求物化，ONNX用asset-objects链接；staging与最终单根验收 | baseline/mod也走统一入口；资源集合首次流式复制并校验、后续复用；请求写原程序/身份/provenance和marker；移除新发布逐请求模型链接路径，既有asset-objects及旧完整树保持可读且受保护 |
| native/recognition/service.cpp:61–73、319–335 | 无lease时单根构造；root/revision断言；OCR校hash后拼root/model目录 | 无lease也能读新格式；保持原一致性断言；det/rec/keys全部用lease解析，不只修ONNX |
| native/recognition/ocr.cpp:33–55、87–105及.hpp | OcrEngine用model_root拼三文件；files_持租约；RunOcrModels按model.dump()复用 | 优先传三个已核成员路径和组合租约；若保留目录接口，证明三成员同父目录且属于同共享集合。整个模型生存期持租约，模型销毁后才释放。保留同run两语言槽、上一Service释放后复用的原门禁 |
| native/games/wvd/vision/native_asset_resolver.cpp:38–67 | require_member、lease.bytes，缓存key含revision/lease.identity/path | 保持此路径；不改为从共享根直接imread。重复物理内容不消除原请求身份隔离 |
| native/runtime/native_run_coordinator.cpp:224–266、413–442 | 核program/bundle revision；NativeUnit→Service→Session；run定义保留request、program/pack revision | 保持校验与接管边界；组合lease沿原对象传递；可附加资源引用诊断，但不替换旧run定义字段，不将result_saved视为全部引用释放 |
| native/app/application.cpp:945–947、1829–1916 | 准备门查published/asset-objects/cache一层pending；批次扫run与请求子树 | 加共享集合与相关事务层pending检查，创建和复用都必须执行；预算显式纳入共享根，详见第3节 |
| native/app/application.cpp:398–428、468–478、1074、2513 | 作者资源先freeze_manifest_members进asset-cache；semantic/public-flows与头像API直接拼author_bundle.root；自定义资源快照单根 | 这是已冻结作者缓存/预览链，不是published资源读取。保持生成及配置权威；若让这些函数接收组合Bundle，三个直接读取（476/478/2513）必须同步改lease.bytes。不得在共享发布切换中偷偷改author_bundle.root指向共享根；原单根构造仍经统一格式判别 |
| native/storage/run_store.cpp:364–385、670–678、887起 | 写run定义/终态/诊断，校帧pack_revision；不从published取资源 | 不需新数据权威或迁移。保留run/result/帧身份与查询；C09才能决定历史归档承接 |
| tests/native/test_native_author.cpp:1090、1205、1262三段录帧CLI | 递归扫历史root、自算hash，只核source_files，使用人工recorded revision | 三段共同改统一历史加载器，正确承接mod覆盖与真实program revision；不能将marker当素材、遗漏共享payload或伪造录帧程序身份。CLI参数/原图断言保持 |
| tests/native/test_native_publication.cpp:94–115；test_native_application.cpp:1131–1146 | 测试用root/member哈希及同ID断言 | 改用组合成员解析核同SHA及固定共享对象；旧完整布局读取/锁定测试仍保留，不删掉相同业务断言 |
| tools/inventory_publications.ps1:56–64、90–134 | 每个直接目录当请求；resume仅接受合法request ID；run.json仅建request引用；active lease未知 | 明确request、.asset-sets、asset-objects、staging/事务、unknown类型及各自恢复格式；通过marker建立request→digest关系，再连run/request历史，不能以共享目录无同名request判孤儿。无法核完整引用保持unknown，不增加删除权限 |

其他test_native_author中root/member写入为独立单根夹具生成（655/666、2376/2378、3038、3131/3132、3184等），不应全部改共享布局；2378/2401/3520之类从source Bundle取图/复制若覆盖新Bundle测试输入，同样须使用lease成员，不可把测试旧物理布局假设带进新验收。

RunOcrModels可让先前unit的组合lease继续存在，即使新Service使用另一请求对象；这不是泄漏证明。必须等该模型、Service、Session、准备对象、NativeRunDefinition、预览与批次冻结源均退出再判活动引用解除。保持worker_definition_released、worker_joined、batch_payloads_released各自边界；未结输入、恢复计划、history仍引用时不得回收。新共享缓存一旦保留须入持久计量。失败只能撤本次拥有且未接管/未被别人引用的目录；不能因当前请求失败连带删共享对象。

## 3 两种计量及原消费者

`retained_reference_bytes`继续累计该批每次请求的**逻辑成员大小+该轮run事实大小**；新物理请求只留元数据后也必须加回资源逻辑引用，不能改成扫描小请求树后的数。控制文件新增字节另列或明确纳入新字段，不重解释旧已返回值。原累积行为是路径逻辑量，不是按SHA或FileID去重。

冻结新增状态字段：`retained_budget_bytes`表示本批唯一资源集合（同digest实际已核对象计一次）+每请求独立文件+run/诊断增量+相关保留事务/缓存的保守内容预算；`retained_budget_basis="unique_resource_sets_request_run_v1"`；`batch_storage_reserve_bytes=2147483648`。旧`batch_storage_limit_bytes=17179869184`保持。预算unknown/溢出/重解析/未闭合清单必须拒绝，不取0。实际同SHA独立文件对象不扣重；为保守准入可按payload逻辑量计集合内别名，不把它报告为FileID实占。簇分配、唯一文件内容量、可回收量另外报告或unknown。

Application是预算唯一执行者：每轮结束更新逻辑累计与保留预算；硬上限16GiB，继续前必须保留量≤14GiB，且下一轮新增资源/元数据/事务成本进入准入。首次/不同资源集合单独增加；已验证同集合不再按请求重复加大字节。独立的卷空间保护仍为可用≥2GiB+本次最坏新增物化/事务量。共享根不在请求树中不可漏计；已保留的未引用缓存不能从磁盘预算凭空消失。批次预算和持久缓存全局库存是不同统计，不能用“本批没创建”解释为免费。

字段消费核查：`application.cpp:1503`通过run_status原JSON原样公开repeat，路由2537；1865/1866为现有两字段唯一源码写入。`web/src/api/types.ts:121`的repeat类型目前不声明这两个字节字段；stores/runStatus.ts、useRunSession.ts及工作台只消费state/active/reason/count，不据字节字段计算准入。可在原类型补可选新旧字段说明，不需新UI/设置。定向搜索next/tools、next/tests/native及web未发现旧字段数值运算消费者；已有`.local/.../live-and-receipts.json`只是历史快照，保持原字节。inventory_publications独立目录统计须更新上述分类，不能拿其静态引用数替代活动租约或批次权威。

## 4 200目标的实际输入与限制

本次仅从当前批run1对应请求读取identity/flow文本，对identity列出的599个资源做文件大小stat（不读取图片/模型内容）；未重新核599个资源SHA。请求为`giant-200-t0-20261025-b0ad41332a754a33b4d900f14f3b2424`，正式根见`.local/giant-200-t0-261025/first-success.json`。program revision=`1eaf87e273daf69b1a044db09eb5a1e2b9230d9f34576348248f9a9802e6173b`；source revision=`ae872c30e2608fac2075d727c84c8214c0d84dc40eb49712a9b659ff0aedbfa7`。

| 输入 | 实测字节/依据 |
| --- | --- |
| 完整最终资源逻辑量R | 114,864,230；599项，不因zh-Hant剔除英文模型 |
| flow.json | 14,474,791；SHA256 `2a0bad8a35e5ecc5b1409057f6d9690e894009e4c043eb82c96816d0729109fa` |
| identity.json | 15,596,789；SHA256 `702b8fc43ff8adc55a9b6a5e396bb64b08c800207aa706c79d5641310d36b426` |
| 每请求原元数据M | 30,071,580；本请求无image-sources文件 |
| det两个逻辑别名 | 各2,423,224；锁定SHA以ocr-models.json为准 |
| 英文rec/keys | 8,956,702 / 190 |
| 繁中rec/keys | 84,577,022 / 74,012 |

冻结文本并不“小”：新加载器/工具不能照抄inventory的4MiB JSON上限去读identity；当前已存在15.6MB身份及14.5MBflow必须可读。限制应按现有真实程序接受能力及显式总预算设定，marker/shared manifest的小控制限额与大程序文件分开，不无限制读未知文件。

固定同一输入假设下，旧200份资源+元数据为28,987,162,000字节，已超过16GiB；新`R+200M=6,129,180,230`字节（约5.708GiB），尚未包含新控制文件、run事实、诊断、事务、簇开销。第199轮续轮14GiB条件可供其余内容的余量为8,933,276,886字节；第200轮16GiB硬上限余量为11,050,688,954字节。它们是预算容纳量，不是未来诊断实际量。C08应以真实固定manifest和程序尺寸加200次小真实发布循环实测增量、链接数、控制开销，并核全批run/诊断预算，不能用小夹具尺寸替代这里的真实输入。

RunStore默认单run诊断上界 `(128+32+16)*8MiB=1,476,395,008` 字节（run_store.hpp:31、cpp:382–385），另有日志与必要事实；200轮各吃满绝不兼容原预算。现合同从未保证各run均达上界仍完成200轮，因此这不是改变限额或降低必要诊断的理由。C08完成条件是具体固定任务输入及实测诊断量下通过预算验证，并保留超限停止；如果要求最坏满额200轮无条件通过，则明确与16GiB冲突，应停在这条要求，不能自调额度。

既有`.local/consolidation-inventory-20261026/live-and-receipts.json`记录40次尝试累计逻辑引用5,775,929,358字节，成功39、run40 UserStopped。不据平均值外推成功200，不把停止归因空间不足。本次未重算40轮全部诊断、不恢复批次/监控。

## 5 C08不可省略的验收与C09边界

复用现有publication、recognition/OCR、application准备/取消/接管、BundleLease及录帧合同入口，增补下列同链断言：

- 旧完整包及旧历史回放仍可读；新包全部逻辑SHA/原revision相同；mod选择、同路径覆盖、无mod、有provenance、两语言/作者版本/头像变化均正确隔离。
- marker存在但未知/坏digest/错revision、共享manifest或payload缺失/多余/换hash、路径逃逸/大小写冲突、两侧重复、错配两个各自合法目录均拒绝；无marker的新半成品不能误作合法完整旧包。
- det/rec/keys及manifest锁定期间替换/rename被拒；真实OCR从组合路径完成识别，bytes/copy/link同源句柄保持；不使用全包兜底掩盖缺依赖。
- 首次复制、共享完成至marker前、marker后至接管前、接管后快速终态、取消/故障/重启、复用命中仍有pending等窗口，原所有权token与双错误持久留证都成立；旧历史/别的请求引用不被清理。
- 200次小真实发布及跨批复用不线性增加大字节或链接，真实输入预算按第4节；逻辑引用继续增长而预算按共享计一次；14GiB继续阈值、16GiB超额、2GiB卷余量+最坏成本、unknown/溢出均有反例。
- inventory新旧报告恢复格式、私有根分类、未知项、历史request/run引用均正确；不授权历史删除。正式旧树迁移不属于C08，仅能在C09历史查询、恢复、防重与原SHA承接验收后按精确计划执行。

C07无待用户选择的替代架构问题。仍未验证项：所有历史marker保留名冲突、完整历史归档消费者、C08并发/崩溃与容量实证；分别由C08碰到具体对象时失败关闭、C09历史合同、C08离线验收负责。实现不能以本审查替代这些证据。
