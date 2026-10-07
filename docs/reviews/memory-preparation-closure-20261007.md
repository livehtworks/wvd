# 准备链闭环与采集预算

## 范围

用户给出一小时处理时间后，继续1c89a08审查包P01–P04。不扩张战斗/ROI/OCR复用策略，不启动游戏任务、不恢复原50轮、不修改正式profile。M01–M03证据保留；修复候选不等于已部署或实机通过。

## 修改与验收

| 项目 | 已落实 | 验收边界 |
| --- | --- | --- |
| P01复制 | 原Held HANDLE/content_mutex内64KiB复制、SHA256、源前后身份及目标长度/哈希；不调用origin.bytes加载模型 | 原模板/bytes消费者保留，无跨轮模型缓存 |
| P01发布 | 本次唯一staging完整校验，释放临时lease后同盘不覆盖rename，最终路径重新冻结 | 不放宽FILE_SHARE_DELETE；只撤回本次未启动目录，不删除旧published |
| P01取消 | 构造/逐块/文件间/rename前后检查原取消源；任务/作者入口共用最终提交门禁 | 实际取消run_id=0、输入0、目录已撤回；已提交Run不据此回滚 |
| P01计时 | 编译、发布/子阶段墙钟及线程CPU，coordinator.start经原RunStore日志记录一次 | 原performance/info开关；实际日志开/关分别1/0条通过，不创造假Run |
| P02冷启动 | 新鲜LifecyclePort观察旧backend；只有明确退出才同步清理成功后重连原绑定 | 活实例复用；offline/观察失败/身份变化/取消/清理失败保留旧owner；initial_lifecycle_plan不变 |
| P03计时 | 多源watch逐项比较 | 实际Vue组件6秒同值新对象轮询持续计时，换run/active切换重置，卸载清理 |
| P04盘点 | 正式published/runs只读枚举目录、文件字节和历史引用 | 不删除、不去重；全部活动租约、簇分配及独占物理占用unknown |

P02测试走真实Application及原生命周期执行器，只有外部MuMu连接/观察适配器隔离；还用真实活动coordinator验证不静止时拒绝。既有设备边界回归通过。没有制造模拟器退出，也不冒充实机恢复验收。

## 准备实测

巨人使用正式profile/公共流程的独立只读副本，与candidate121 run18的程序identity及599个发布文件哈希一致。revision仍为`0a9d6784b9dd6c4fd8ff548b7e70fa8775755cd7fea4c1d793e3a7c0a4d25fff`。

续修前本机对照：编译6.945930秒/线程CPU6.843750秒，发布11.472581秒。找到两处实际重复分配：ImageCollector对每个非模板对象调用返回值式本地化，造成父子JSON子树反复深复制；子流程校验在遍历owner时每项重新get同一个长入口名字。现仅模板调用本地化，入口名借用原JSON字符串，原递归、全部作用域/资源校验和权限检查不变。

单次修后观察：完整编译3.249126秒/线程CPU3.140625秒，发布11.819474秒，其中复制9.751143秒；源113249373B，599文件，模型原缓存0。中间仅第一处修正时编译4.119956秒。三次均使用相同正式profile/资源的隔离副本、程序identity及全部文件哈希与旧候选一致。不同二进制的先后样本不是统计性能基准，也不代表堆跟踪已通过。真实繁中OCR的既有证据保留：锁定模型路径推理NEXT score=0.999081、OCR对象正常析构；此次不重复跑OCR。

增加有界阶段开始/完成观察，覆盖公共库、任务图、Boot、语言资源、程序转换、模型契约、身份、源校验、复制和两次发布校验。正式入口沿原performance/info开关在operation显示，job结束保留小摘要，原RunStore仍只写一次完成记录。隔离探针将每个边界立即flush到phase-events.jsonl，进程中断时能留下最后开始阶段；不保存大JSON副本，不增轮询线程。performance开/关与warn门禁定向检查通过。

实际refresh_images分配检查：256模板叶的平层与外加12层all具有完全相同图片索引；10次扫描分别分配8756320/8757600字节、179880/179900次，嵌套只多1280字节/20次，不再随深度复制整棵叶集合。夹具初次缺合法入口被真实validate拒绝，补完整入口/终点后通过，没有跳过或改弱校验。这测的是分配活动，不是存活字节或历史增长归因。

Windows发布检查覆盖身份/manifest/revision、8MiB模型固定缓冲、取消/游标复位、拒绝覆盖/路径越界、原锁、staging释放/rename、rename前后取消、哈希错误、真实短读和只读句柄写入失败。未制造磁盘耗尽，不声称实测所有设备的正向短写；部分写入由计数循环处理。

最终门禁最初因验证自己的identity输入流未关闭而阻止删除，失败保留；修正夹具流作用域后成功撤回，没有放宽生产锁。M03实际事件接口/分配回归再次通过。

## 跟踪负证据

仅跟踪隔离准备进程PID13268，不执行coordinator.start、设备或游戏操作。PID HeapSnapshots启用后120秒未完成，触发`TRACKED_PREPARATION_EXCEEDED_120_SECONDS`；私有峰值123232256B（117.52MiB），未触发1GiB私有/98%系统提交保护。进程、独有WPR会话和堆配置均已确认关闭，cleanup errors为空。

原超时现场尚无published复制目录，因此不能把120秒解释成整模型文件复制耗时；该历史运行缺阶段记录，不能断言唯一是编译或跟踪库。没有B1/B2和有效净增表。早期回执未存测试EXE哈希，宿主随后重编译，不能据此计算严格同二进制A/B倍率。后续探针已补输入哈希/变化拒绝及上述即时阶段记录。此次管理员请求未启动：首个等待由本轮取消，第二个Start-Process明确报告“操作已被用户取消”，没有证据目录/新ETL；QueryAllTraces未发现自有WvdPrepare会话，没有绕过UAC或关闭外部进程。

第一次长隔离路径在跟踪前失败：目标asset-cache哈希/temp UUID超过测试宿主支持路径长度，源图存在。失败保留，改短根；未重收素材或修改生产longPathAware清单。

M04原候选身份预检通过，但产品采集未执行。无跟踪18秒不能替代跟踪预算，旧480秒准备不能宣称已消除。是否把一小时处理扩为实机采集窗口的澄清尚无回复，原20分钟/512MiB范围未自动扩大。

## 磁盘与剩余项

672目录、392816文件，逻辑63460141750B（59.102GiB），670份关联历史run.json。GetCompressedFileSizeW逐路径之和同值；未压缩/非稀疏时返回逻辑长度，不是簇舍入或去重独占占用。[微软说明](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getcompressedfilesizew)。盘点顺序非原子；首次180秒部分完成，第二次只补未完整目录，保留不同观察窗口。未删除正式包或历史。

当前API不列所有BundleLease根，活动引用全集unknown；历史引用不是活动租约。正式profile SHA仍为`3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C`。原candidate121仍Interrupted/17/50，未恢复。新候选以独立交付回执为准；源码未commit/push。

45.11MiB仍RESOURCE_UNRESOLVED。剩余：同进程batch_payloads_released的B1/B2及主要栈所有者归因、跟踪准备扰动细分、实机冷启动恢复、连续批次持有阶段、自然面具失败原帧关联。不得用Private Bytes、live=0、本次分配次数或磁盘字节代替归因。续修原生服务构建通过：next/build/Release/automationd.exe SHA256为DDDA6B76F4A34CBA6DF39080F373C0204455043799055C4F85AFA1DC66D37F4E；未部署，旧候选回执不回写为新身份。

## 证据

- `next/.local/p01o-stage-result.json`、`p01o-copyfix-result.json`、`p01o-ownerfix-result.json`及对应根下phase-events.jsonl：本轮分段、完整身份/599哈希、最终取消与零输入。
- `next/.local/image-index-final-result.json`、`prep-phase-switch-final/result.json`、`pub-stage-fix/result.json`：资源扫描分配、日志门禁及发布保护；失败初次记录保留在image-index-copyfix.log。
- `next/.local/preparation-product-build.log`：原生服务构建。本轮未执行部署/提交/推送/游戏循环。

- `next/.local/memory-review-1c89a08/p01-commit-verified-result.json`及`next/.local/p01-cgood/commit-observed.json`：完整准备、最终取消。
- `next/.local/p01-pub-final/result.json`、`p01-bundle-regression.log`、`p01-frozen-published-ocr.log`：发布/租约/真实OCR。
- `next/.local/p02-final-data/result.json`及`p02-device-regression.log`：Application、日志开关、设备边界。
- `next/.local/m3p-20261007/result.json`、`p03-ui-final.log`及截图：事件接口和Vue计时。
- `next/.local/p01t-f69c23ac/receipt.json`：跟踪超时及清理；长路径失败在`p01-tracked-c4034edae7d04eb5a28664b3f1209b26/`。
- `next/.local/memory-review-1c89a08/p04-inventory-final.json`：盘点；早期allocated_bytes_by_path字段实际是上述Win32存储字节口径，不改原数值。
- `next/.local/memory-review-1c89a08/release-receipt.json`：新候选构建/预检及是否部署；不以源码验收冒充游戏通过。
