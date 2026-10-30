# 内存归因与收尾修复

## 当前结论

已定位并修复一项实际内存滞留机制：任务对象销毁后，Windows LFH仍保留空闲页。每次真实worker join后回收空闲页，邻近窗口实际降低约11.86至30.23MiB，不销毁活跃对象、不重启服务。另消除了准备图和状态轮询的无效深复制。基线两轮、回收修正版两轮、带HeapSummary的复核两轮均Completed；最终candidate127已部署原17654，当前Idle，没有长循环。

最新同进程两轮HeapSummary实际分配仅增加49370字节，而PrivateUsage增加2895872字节。这排除了“这2.76MiB全部是未释放堆对象”的判断，但未覆盖全部非堆分配所有权，也不能把历史45.11MiB逐字节归因。因此本轮的回收缺口已修复，历史完整资源归因仍保留`RESOURCE_UNRESOLVED`，不宣称整个工程不存在泄漏。

## 为什么以前的日志没能归因

- PrivateUsage、OCR对象计数和图片缓存字节只能确认现象，不能区分活跃分配、堆内部空闲页和其他提交。
- 历史堆快照只覆盖启用后的分配，终点约4.66MiB活跃块不代表进程总活跃分配。原ETL没有完整两个收尾阶段，不能计算可信的长期同阶段分配栈增长。
- 本轮p01t-resolve不是权限失败：管理员助手确实运行，但持续跟踪使任务构建耗时79.560秒、启动恢复构建29.213秒，120秒时仍未准备完成。失败和资源清理回执保留；没有继续用无跟踪耗时冒充跟踪预算。
- CDB -pv会暂停进程；本轮!address超时后曾遗留暂停，已在PID/创建时间核对后恢复。正式工具现在使用-pvr，不暂停目标；地址枚举改用VirtualQueryEx，首次实机读取约0.22秒。

## 已落地修复

1. 语言探针无需替换时返回nullopt，不复制整棵JSON；资源收集、编译及原生识别调用方全部按同一契约承接。完整2561节点/599文件与原candidate121身份和摘要一致。
2. 新鲜设备观察的10秒期限在成功或异常离开观察作用域时清除。candidate122旧现场在首个输入前失败的证据保留；candidate123已真实确认VPN、启动游戏并完成两轮。
3. NativeRunCoordinator只在真正join工作线程后调用Windows HeapOptimizeResources，保留维护前worker_joined和新增heap_resources_optimized；记录API成功/错误、耗时和前后私有提交。重复查询终态不重复回收，不清除活跃对象，不靠重启服务降内存。
4. 低扰动工具绑定PID、创建时间、服务实例、run、EXE/PDB和batch_payloads_released，权威data外保存receipt、堆摘要和区间元数据。5秒/32768区间、整次30秒/16MiB上限，无游戏输入、无进程注入。
5. 工作台状态接口原先每次复制整份active_pipeline_to_node_/active_source_paths_。改为在已有互斥锁内只读取所需节点，不改变返回字段、事件顺序和可见步骤。真实Application状态接口在1个索引与额外2561个无关索引下，十次查询的分配均为83624250字节/1037840次，结果一致且临时响应销毁配对；这是分配流量修复，不是长期泄漏证明。candidate127在126两轮完成后已部署。
6. 现有analyze_memory_owners.py增加同进程、同边界两轮对比；核对PID/创建时间、协调器、run先后和有效统计，缺采样不填0，跨进程拒绝。原真实两轮输出与人工重算一致，身份/顺序/缺失/不完整负条件已核对；报告仍明确缺逐所有者分配栈，不因数字齐全就宣布全部归因。

## 实机证据

正式profile SHA256保持`3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C`。

基线candidate123：PID8056，服务48B6DBC7-28F8-446B-85EA-1AD8F7468DBC，协调器84454656-AF36-48DC-A06A-D3F6B1AEB3A9。run1/2均Completed，第二轮471.219秒。首次包含冷启动及旧现场收尾，游戏随机战斗/宝箱也不同，不能将两轮时长差当成优化收益。

| 同阶段，字节 | 基线run1 | 基线run2 | 增量 |
| --- | ---: | ---: | ---: |
| worker_joined | 64905216 | 76562432 | 11657216 |
| batch_payloads_released | 57139200 | 66756608 | 9617408 |

基线第二轮的VirtualQueryEx私有提交区间64999424字节；随后进程PrivateUsage70184960字节。图像/映射区间另列，不等于全部私有占用。!heap主堆Commit162800KiB、Free111458KiB是内部统计，与实际PrivateUsage不一致；禁止直接据此计算泄漏或可回收字节。

修正版candidate125：PID50320，服务325F08FB-2699-44FB-A2B2-64350344C147，协调器16241470-1428-4DAF-AD92-E9B614752436；原17654/原data。EXE SHA256 `EA46693EF3A778435102AC9F1187975F396EF507109156E8B16F95B3658B9592`，冻结PDB SHA256 `6F589EBA2DD16FAA3D7323E741B66B995687C77A64F1165C0B0338956E531C91`。candidate124仅打包，未部署；其日志检查发现旧用例没有接受真实系统压力分支，已按生产90%阈值和日志级别修正断言后重建125。

| 修正版，字节 | run1 | run2 |
| --- | ---: | ---: |
| worker_joined | 63496192 | 81022976 |
| 紧邻HeapOptimizeResources前 | 58040320 | 79003648 |
| 紧邻HeapOptimizeResources后 | 43003904 | 47308800 |
| API窗口内下降 | 15036416 | 31694848 |
| batch_payloads_released | 43110400 | 47423488 |
| API耗时 | 3581微秒 | 6296微秒 |

run1/2真实完整完成，分别360.671/407.037秒。API前后是同进程邻近采样，不跨重启比较；后台线程仍可能有少量活动，不将所有差值伪装成逐块释放账本。新工具枚举1812个区间用时5毫秒，整个读取2.401秒。两轮batch_payloads_released差4313088字节，尚不能证明全增长已解决。

candidate126保持相同回收逻辑，新增动态加载的Windows HeapSummary：记录回收前后allocated/committed/reserved、堆数量、完整性和统计耗时；缺API时记录不可用，不以0冒充。PID52220、服务B401DF05-8207-497A-8183-A26B69F0472D、创建时间134358285720054744。EXE SHA256 `F7996D297EF9361F4577D6EA7BBAE81DEA44AEEC394A3B74B878BD976CA69CF6`，冻结PDB SHA256 `0F7F2E07E52EEB3516BAA36D4A93EA25573FDD2D0FED5578ACBF2E4E61CAA3C5`。请求f88d0433-5091-4359-b15f-f77e85c56970，协调器04CCA003-B536-465F-AE7A-69CCE56DD711，两轮已完整结束；从请求文件到最后边界891.786秒，未开启WPR，符合该次20分钟有限复核窗口。

| candidate126同worker join维护阶段，字节 | run1 | run2 | 差 |
| --- | ---: | ---: | ---: |
| HeapSummary allocated | 17313175 | 17362545 | 49370 |
| API后PrivateUsage | 43036672 | 45932544 | 2895872 |
| API前PrivateUsage | 55476224 | 64643072 | 9166848 |
| API前后下降 | 12439552 | 18710528 | 不跨轮推导收益 |
| 内部heap committed（非PrivateUsage） | 99717120 | 157605888 | 不作为泄漏量 |

两轮elapsed_seconds为421.880/408.274，均完成3个业务段、details_complete/quiescent=true、secondary_errors空。3个识别服务/OCR/会话每轮对应销毁，第二轮累计各6个创建/销毁、live/ready=0。每次回收前后allocated一致。第一轮处于同一批次续轮，没有batch_payloads_released；表格严格比较共同的heap_resources_optimized，不把缺失补0。第二轮最终batch_payloads_released为46055424字节。

最终candidate127包含相同回收/HeapSummary及状态索引复制修复；原17654/原data、PID28728、服务CDA71E2A-D0EA-475E-BCE0-12C66EC7D12E、创建134358297889084589。EXE SHA256 `29945ECB9A5FC01BC0B94351681053CCD07D59ECB6805D910CEF8E8DA313AB9E`，冻结PDB SHA256 `B010F0E50CF88849E2AD2DA35697E3647B95A78D708C591FE9B2CD03EDBD57EE`。只在126两轮结束后经manage_service整体部署；127实际服务Idle/不busy/静止，未另跑游戏。分析脚本在源码工具目录执行，不新增后台服务。

## 缩小来源范围

- 真实OCR初始化、NEXT识别、取消及销毁连续四次：HeapSummary实际分配均为7523762字节；回收后PrivateUsage为15818752/15855616/15872000/15921152字节。说明该固定OCR生命周期没有观察到累计活跃堆分配，不推广为所有游戏图片/动态形状均无泄漏。
- 完整巨人准备/发布/取消四次在独立目录执行，每次核对原程序身份及全部599文件哈希，模型缓冲0、游戏输入0、未启动发布已撤回。回收后实际分配6745344/6751344/6758088/6765576字节；测试本身还保留每轮JSON结果，20KiB差不能直接归为生产泄漏。PrivateUsage为17330176/17145856/17756160/18280448字节。
- HeapSummary的堆内部committed并非进程PrivateUsage：OCR实际分配不变时，该值仍在24236032至30887936间变动。不能再用堆内部Commit减Free作为实际可归还内存。
- candidate125第二轮期间，系统提交约54.98GB升至61.25GB。14:06:19新启动的MuMu实例0（PID47356）可读私有提交约3.46至4.34GB；本次操作的是实例2。可读进程统计并不覆盖全部系统提交，不能将整机压力都归于WVD；没有关闭该其他实例。

## 已失败的方法

- 对具体主堆执行实时!heap -stat超过30秒，停止自有调试助手；后续不重复该方法。
- Microsoft签名ProcDump的-mini plus克隆仅144618字节，虽打印完成，CDB提示目标初始化不完整，没有有效堆数据，不可用作归因。
- 同一空闲进程的完整PSS克隆约1.1秒产生200748774字节转储；堆摘要可读，但离线逐块统计仍超过60秒，部分堆报无效签名。保留原文件，不声称已有可信分配大小表/分配栈。
- 原始转储只在忽略的next/.local保存，可能包含用户配置和进程数据，不提交、不上传。使用HeapSummary补低扰动实际分配计数，不将计数冒充分配栈。

## 验证与证据索引

- 实际Windows API、活跃65536字节内容保持、同轮重复collect不重复维护、四个生命周期槽、运行事件序列和真实系统压力分类：`next/.local/memory125-boundary-test.log`通过。
- 准备身份/取消：`next/.local/p01-no-copy-result.json`；观察期限检查：`next/.local/p02-read-window/`。
- HeapSummary边界：`next/.local/memory126-boundary-test.log`；真实OCR四次：`next/.local/memory126-ocr-cycles.log`；真实准备四次：`next/.local/memory126-prep-result.json`；状态接口：`next/.local/memory127-status-test/status-index-allocations.json`。只跑对应受影响链，未执行全工程回归。
- 最后两轮的原始正式结果与生命周期：`next/.local/c11-flow-product/data/runs/04CCA003-B536-465F-AE7A-69CCE56DD711/{1,2}/`。独立摘要`next/.local/memory126-layout/comparison.json`及现有分析器输出`ownership-comparison-final.json`；部署回执`next/.local/memory127-deploy.log`。
- 跟踪负证据：`next/.local/p01t-resolve/`；失败的-pv读取和恢复：`next/.local/memory123-layout/after-1/`、`resume-after-layout-timeout.log`。
- 有效基线短读取：`next/.local/memory123-layout/after-1-short/`、`after-2/`和`after-2-regions.json`。
- 修正版读取：`next/.local/memory125-layout/after-1/`。本次`-h 0`没有产生请求的分配大小表，因此只认可堆摘要和地址区间，不能认可allocation size证据；该回执原值保留，工具已改为显式主堆地址并增加表存在性检查。
- `memory123-layout/window.json`从旧122复制，时间已过期，不用于证明后续游戏请求都在该20分钟内。本轮没有新增连续WPR，实际短读取各有独立计时；不回写原失败为成功。

## 还不能宣称的内容

当前两轮收尾差分和回收不能证明历史长周期增长全部闭合，也不能证明没有其他活跃对象泄漏。新两轮已将“多MiB私有提交增长”与“约48KiB实际堆分配差”区分；后者可能包含不同事件环内容和缓存，缺分配栈不能指定到某个所有者。HeapSummary不覆盖全部独立VirtualAlloc所有权。已接入自动差分，后续若真实同阶段allocated持续累计，应以该分支定位所有者；不能再原样重跑本轮已失败的全量堆遍历。没有自动开启50/100轮来替代归因，也未结束用户其他程序。

上述诊断收口时尚未提交推送。用户后续已明确授权commit/push/部署及巨人50轮，本轮按此执行，保留用户`.vscode/`修改；批次启动身份另记当前事实。回滚使用保留的旧候选及未变化的正式profile，通过原服务管理入口整体部署，不保留双后台或自动回退。

## 官方资料

- [HeapSetInformation](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation)
- [HeapSummary](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsummary)
- [ProcDump](https://learn.microsoft.com/en-us/sysinternals/downloads/procdump)
- [VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex)
- [WinDbg heap](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-heap)
- [WPR Heap Snapshot](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot)
- [UMDH差分](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/using-umdh-to-find-a-user-mode-memory-leak)
- [VMMap](https://learn.microsoft.com/en-us/sysinternals/downloads/vmmap)
