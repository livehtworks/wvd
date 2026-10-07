# 内存增长归因执行计划

目标：定位原同阶段45.11MiB增长的主要来源，修复实际持有或释放问题，并用相同负载/收尾阶段复核。准备性能优化或日志通过不作为内存解决结论。

## 执行顺序与状态

1. 已完成：复核现有证据并查微软Heap Snapshot、UMDH、VMMap资料。进程提交增长、仍存活堆块增长和分配活动分别计量。
2. 已执行，未通过跟踪预算：p01t-resolve实测任务构建79.560秒、启动恢复29.213秒，120秒时仍在语言资源阶段；自有进程和跟踪均已清理。不是权限失败，不原样重跑。无需语言替换时的JSON复制也已消除；完整2561节点/599文件与candidate121身份及哈希相同，无跟踪编译3.215秒。
3. 已完成两轮实跑基线：candidate122首次输入前暴露观察10秒期限泄漏，已修复并由candidate123承接。candidate123/PID8056的run1/2均Completed，第二轮471.219秒；同batch_payloads_released分别57139200/66756608字节，增长9617408字节。第一轮包含冷启动和旧现场收尾，不把时长当成等负载对照。未追加WPR采集，采用收尾短读取。
4. 已取得实跑差分：!heap内部Commit与PrivateUsage不一致，不能直接相减归因。VirtualQueryEx区间约5至6毫秒；实时及离线逐块堆遍历仍超时，改用HeapSummary。真实两轮实际allocated仅+49370字节，PrivateUsage+2895872字节；每次维护前后allocated不变而私有提交下降11.86/17.84MiB，确认空闲堆页滞留及回收效果。固定OCR四次值完全相同，独立准备四次身份/599哈希/取消正确。未证明全部非堆所有权或历史45.11MiB的逐项归因，不猜成OpenCV泄漏。
5. 已实现：消除无语言替换时的深复制，并在真正worker join后接入HeapOptimizeResources空闲页回收；保留回收前后独立样本及耗时，不覆盖原始增长。真实Windows API、活跃内存内容不变、重复collect不重做、诊断日志压力分类的针对性检查通过。
6. 实跑完成并已部署：candidate125、126各两轮Completed。126两轮窗口891.786秒；完整业务段/静止/详细结果通过，实际堆分配差与总提交差已区分。最终candidate127另修状态轮询全图复制，实际接口针对性检查通过并部署原17654/原data（PID28728）。当前Idle，无新增游戏循环，原候选和profile保留可人工回滚。
7. 已更新：当前事实、执行注意项、报告和原分析器同阶段自动差分记录实际结果及未归因边界；本轮未另行要求commit/push，不自动提交用户修改。没有扩大50/100轮。RESOURCE_UNRESOLVED仅保留未取得逐项所有权的历史增长，不把已修复的空闲页回收或有效两轮复核标成未执行。

## 采集约束与完成标准

- 沿用现有方案、VPN和真实任务入口；不消费绿色/紫色宝石，不制造死亡、断网或模拟器故障。
- 每次产品跟踪窗口最多两轮/20分钟/512MiB；启动前以实测开销核算。采集失败需解释具体原因并修改方法，不能原样反复跑；若两轮无法容纳，先减少观测开销或使用同阶段的可比较窗口，不伪造完整性。
- 管理员请求由Windows UAC确认，明确取消即停止该次提升，不绕过；可并行推进无需提升的分析。
- 原始ETL、回执和失败记录保留，证据写入next/.local独立目录；应用正式profile不为测试改写。
- memory123-layout/window.json是从candidate122复制的旧窗口记录，不能证明后续两轮都在原20分钟内。两轮是分开的有限游戏请求、没有持续跟踪：实际短读取各自有时间回执；失败的!address读取单列。后续不沿用这份过期窗口冒充准入。
- 完成至少需要：有效前后差分、主要增长来源及对应代码、修复后的同条件结果；仅Private Bytes降低、对象计数归零或短时无增长不足以宣告泄漏解决。

## 官方依据

- [WPR Heap Snapshot](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot)：按PID启用后记录分配栈，SingleSnapshot导出快照，结束后关闭配置。
- [UMDH前后分配栈差分](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/using-umdh-to-find-a-user-mode-memory-leak)：需匹配符号及两个时点，比较仍存活分配的变化；单张快照不足以确认泄漏。
- [VMMap](https://learn.microsoft.com/en-us/sysinternals/downloads/vmmap)：区分进程提交内存类型、工作集及详细地址布局，可导出保存。
- [HeapSetInformation](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation)：HeapOptimizeResources针对LFH缓存，在可能时反提交空闲页；不等于释放活跃对象。
- [VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex)：读取连续区间状态与类型，MEM_PRIVATE/MAPPED/IMAGE的提交区间不冒充用户堆活跃块或分配栈。
- [HeapSummary](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsummary)：区分堆allocated/committed/reserved，动态检测API；数字仍不等于调用栈。
