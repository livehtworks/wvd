# 内存采集方案整改

## 当前结论

采集/解析工具已接入 `next/tools/`，不再依赖上一轮 `.local/collect-wpr.ps1` 临时脚本。源码基线为 `e741924`，本轮只改采集工具和事实文档，未重新构建或部署产品。用户已授权本轮提交并推送个人`fork`，不含正式配置、运行产物及无关`.vscode`。空闲probe阶段没有游戏输入；用户后续已通过正式入口启动candidate121新50轮，实跑诊断不能再描述为后台Idle。

空闲后台的有限协议采集已经获得完整ETL、目标堆快照和可解析的应用调用栈，**但实跑全系统VA Monitor因容量失败，仅堆Monitor又因20分钟到期缺第二个join，历史45.11MiB增长仍未归因。** Collect必须显式提供游戏请求和`-AllowGameTasks`，本轮没有调用；Monitor只读观察已运行50轮，不提交或停止游戏。最新仅堆窗口已关闭：19分57秒、41.814MiB、无已记录丢失，只有当前部分轮收尾；部分导出2.071GiB超128MiB失败，不算完整差分。活动文件保护已改为FileStream/退出复核，尚未再次跟踪或导出验证。见[实跑窗口报告](memory-monitor-two-20261007.md)。

## 旧方案为什么不够

- 默认Heap + VirtualAllocation profile额外包含JScript/.NET/其他事件及系统状态；VirtualAlloc本身全系统记录。128MiB临时阈值仅按容量粗略预留，不能证明吞吐、单个采集器上限或有效游戏窗口。
- 136897丢事件是原WPR保存时的事实，具体丢失源无法从残缺ETL还原。不能仅据数量判定是目标进程泄漏、磁盘慢或某个缓冲不足。
- 本轮短采集发现16MiB内核Sequential文件过早封顶。事件提供者进程名过滤与全局VirtualAlloc栈同时启用，仍有全局记录；仅改为提供者Stack=true时，导出目标VAlloc栈缺失。两种均未放行。
- WPA exporter新版的`-symbols`是 **Event Tracing for Windows处理器的参数**；只设置`_NT_SYMBOL_PATH`不会启动符号解码。命令退出0也可能有局部导出错误，必须核对实际CSV、字段和目标栈。

## 新的唯一采集链

1. `collect_memory_stacks.ps1`核对正式服务EXE、PID、创建FILETIME、instance、data和匹配PDB。Preflight/Probe/Collect要求后台空闲；Monitor要求既有active批次的request_id匹配且禁止提交参数，不拥有原任务。预检不启动跟踪，Collect只走既有正式任务入口。
2. 使用WPR **PID Heap Snapshot**，只在边界导出仍存活的堆块和分配栈，不连续导出每次堆分配/释放。不改IFEO、不冷启动后台；只覆盖启用后的分配栈，原先未跟踪的块明确不覆盖，不拿来复现candidate119历史全部增长。
3. HeapAndVirtualAlloc profile保留ProcessThread/Loader/VirtualAllocation及HeapSnapshot事件。VirtualAlloc仍是全系统事件集，**没有伪称录制阶段只含目标PID**；使用16MiB栈缓存去重，分析时严格选择冻结PID。用户指定HeapSnapshots时用XML结构化移除VA关键字/栈和导出视图，只保留PID堆快照及模块/进程身份信息；不将缺VA包装成完整内存归因。
4. ETW缓冲固定内核32MiB、Snapshot64MiB，栈缓存16MiB；另有目标进程快照数据库开销，不能把被诊断进程的受跟踪内存当无开销基线。Sequential文件上限当前为内核96MiB/快照64MiB，分别在92/60MiB提前保存，合计152MiB保护；为最终合并留空间，总跟踪上限仍512MiB，不用Circular覆盖早期边界。
5. 使用Win32 `QueryAllTracesW/ControlTraceW`读取本次两个采集器的EventsLost、LogBuffersLost、RealTimeBuffersLost、缓冲与写入量；实时有丢失/采集器消失/身份变化/98%系统提交压力即停止本次采集。文件长度使用实际打开的文件流；保存使用`-compress -skipPdbGen`，不再为无关托管模块生成动态PDB。
6. 同一PID/创建时间只有一个helper持有互斥锁。拒绝他人的WPR/快照配置，只清理本次会话和PID配置。WPR子命令有等待期限；退出时核对跟踪、快照配置和本次任务停止结果，不能全局取消他人的诊断。
7. Collect将最多两轮拆为两个**各1轮的正式提交**。上一轮Completed/quiescent、repeat完成且`memory-lifecycle.json`真实worker_joined存在后，才取快照和提交下一轮，避免连续repeat自动启动令快照跨轮。提交数量、实际启动数量、完成数量分列；没有启动不计轮。依旧最多20分钟，提前预留120秒保存/清理，不改游戏重试、付款或业务流程。
8. `export_memory_stacks.ps1`通过当前SDK处理器参数加载本地匹配应用PDB，核对两张表、PID、快照instance数量和每个栈。生成`heap-by-stack.csv`、相邻边界`heap-diff.csv`、目标`virtual-alloc-target.csv`及摘要；区分活动块差与提交页事件，不相加。原Profile和导出Profile在新证据目录冻结并记录SHA256，后续源码变动不能静默改分析契约。导出另限120秒/128MiB文件，失败保留部分输出，不当完整证据。
9. Monitor在真实Completed/quiescent/worker_joined边界取样并复核没有跨入下一轮，不暂停批次。默认首个join为基线；`MonitorCurrentRound`则首个join标`worker_joined_1_partial`、次个join为完整下一轮。首个部分窗口不能计作完整轮次。HeapSnapshots模式仅核对和导出堆表，保存/清理提前预留30秒；全模式仍预留120秒。各模式都受本次绝对截止及总跟踪容量约束。

## 实际验证

证据根：`D:/programcode/python/wvd/next/.local/memory-stacks-repair/`。保留失败probe1/2、缺VAlloc栈的probe3及最终probe4，不覆盖旧工作包437MiB ETL/receipt。

| 实际项 | 结果 |
| --- | --- |
| WPRP本机XSD及WPR解析 | 通过 |
| EXE/PDB GUID/Age/哈希 | candidate121匹配，未替换历史符号 |
| probe4实际跟踪与保存 | 27.25秒含启动/保存；实际观察窗口约5.55秒；ETL10,757,899字节（约10.26MiB） |
| 事件完整性 | 实时两个采集器丢事件/缓冲均0，stop无丢失提示，xperf统计和WPA解析接受 |
| 堆快照 | 2个instance、33/35个活动块、11584/12942字节；成功对应before_tasks/idle_probe |
| 应用分配栈 | Heap差分的+2块/+1358字节落在HttpServer::Impl::accept/Asio调用链；仅为空闲API采样窗口事实，**不是泄漏结论** |
| VirtualAlloc | 目标17条，有真实栈；至少3条包含已解析应用源码函数，无Symbols disabled |
| 字节差分与来源 | 两张表正式导出及按栈差分通过；缺系统DLL/CRT私有符号在analysis-summary逐项保留 |
| 现场与清理 | 服务Idle/quiescent，WPR无录制，PID38436快照关闭，profile前后哈希不变 |
| 游戏轮次 | **0，未输入游戏** |

最终probe4使用内核64MiB上限，之后调整到96MiB并完善冻结、单helper互斥和任务停止归属；这些空闲采集/解析事实来自probe4版本，不冒充新源码所有失败分支已实机触发。后续实跑Monitor已取得一次真实部分轮worker_joined，未取得两轮完整差分；启用跟踪前的存活块、系统DLL缺符号及历史119原因仍未覆盖，不能沿用空闲probe声称实际负载通过。

## 使用与安全边界

需PowerShell7，Capture需要管理员权限。路径始终显式指定，不使用默认正式data写测试结构。

```powershell
next/tools/collect_memory_stacks.ps1 -Mode Preflight -EvidenceRoot <新的独立证据目录> -CandidateRoot <部署候选目录> -DataRoot <正式数据目录> -PdbPath <匹配PDB>
```

Probe使用相同参数及`-Mode Probe`，不启动任务。新授权下Collect再增加`-Mode Collect -TaskRequestPath <请求JSON> -AllowGameTasks`；请求必须为`task_id=GiantBounty`、`resource_locale=zh-Hant`，沿用既有正式方案/VPN，脚本固定各1轮、最多2轮，不写profile或公共流程。取消/重启等改变正常观察窗口就停止收集，不制造异常、不消费宝石、不卖装备、不抽卡。

保存后执行 `next/tools/export_memory_stacks.ps1 -EvidenceRoot <本次目录> -PdbPath <匹配PDB>`。只用本地PDB，不隐式下载其他进程的全部符号。若关键系统/ORT栈无法辨认，列missing而不是把unknown余额强归给框架。

## 官方依据

- [WPR Heap Snapshot](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot)：PID启用及边界快照，启用前分配栈不覆盖。
- [MaximumFileSize](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/maximumfilesize)：Sequential封顶、Circular覆盖区别。
- [StackCaching](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/stackcaching)：重复栈缓存，CacheSize单位KB。
- [ETW会话与丢事件](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/sessions)：吞吐/缓冲与EventsLost，不能用丢失记录算可靠释放差分。
- 本机WPA exporter11.7.383.39833的`-help 'Event Tracing for Windows'`和实际导出日志是处理器参数依据，不照搬旧版CLI位置。
