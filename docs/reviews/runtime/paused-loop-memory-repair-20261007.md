# 暂停循环后的内存与技能修复

## 范围与现场

- 用户要求暂停当前循环并修复已发现问题，不再自动续跑。
- 原candidate128进程36736、协调器097D793A-C476-4B88-B5B6-5034808C4236：完成28/50，第29轮由正式停止接口进入UserStopped/STOP_REQUESTED，不是业务失败。
- 未点击游戏、未重启游戏或模拟器、未改变VPN或正式profile；profile SHA256仍为3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C。
- 原日志及失败图不改写。末次动作图另存next/.local/giant128-50/paused29-last-action.png，防止历史滚动覆盖。

## 已确认原因与修改

### 停止后的收尾竞态

watch_task_session在收到cancel后退出等待，调用record_batch_release时工作线程可能尚未写完终态；该函数直接返回，随后监视线程也退出，后续没有执行join及空闲堆回收。正式29轮只留下worker_definition_released，停止后私有提交仍为280637440字节（267.6MiB）。

现在取消只结束调度：原监视线程继续等待工作线程终态，再join和释放批次捕获；不在控制API线程等待，不创建第二个生命周期所有者。隔离验证刻意暂缓原生产worker收尾，确认停止API仍响应、busy仍覆盖清理窗口，放行后四个生命周期边界齐全、UserStopped且输入0。原29轮证据不补写。

### 技能灰色不可用仍连续补点

第3/5/24轮失败原图均是面具本人，左上技能灰色；同行右上技能亮字。原输入坐标正确，不能归因于人物错认。原逻辑在正常同角色菜单上补点约21秒后才防御，未将该动作误记为施放成功。

新增正常菜单内的技能灰态观察：检测标签内部亮字/边缘，排除边框、符号及明细按钮；必须有亮字同伴且目标标签确有暗字边缘。同一角色、无弹窗菜单连续两帧确认后直接走原手动防御，仍不消费配置技能，不启用Auto。整屏变暗、空标签、无法证明不可用时保留原等待，不能仅凭SP数值猜消耗，也不缩短慢网络的通用等待。

## 内存归因与改善

正式前27轮同一进程、同一维护边界：私有提交38.65→63.28MiB，但实际堆分配只增加179.88KiB。这不支持直接把24.63MiB全部称为仍存活的OpenCV对象。

1. 真实第27轮事件流通过生产EventJournal回放6次：每次2713事件、84次读取序列化；收尾约5MiB，未复现原增长。HeapSummary前后进程私有提交相同，没有证明统计自身制造增长。仅覆盖日志环，不是整场游戏回放。
2. 实际繁中模型与真实战斗原帧，按900×1000、900×450、450×180三个区域初始化、推理、销毁4次：旧堆策略实际分配每次7922618字节，私有提交17960960→18075648。最终Segment候选第二至第四次实际分配均7917642、私有提交均16232448字节。未复现明显OCR存活分配累积；不推广成所有输入已验证。
3. 完整当前巨人编译、599项资源发布、哈希/身份复核、提交前取消，独立4次准备，输入0。只读复制正式配置及公共流程，临时发布目录独立且按原取消逻辑撤回。两组相同EXE与DLL，仅Windows堆manifest不同，未更换OCR/OpenCV/C++运行架构。

| 准备轮 | 原NT堆维护后私有提交 | Segment Heap维护后私有提交 |
| --- | ---: | ---: |
| 1 | 64417792 | 48627712 |
| 2 | 64569344 | 49180672 |
| 3 | 65269760 | 58105856 |
| 4 | 65454080 | 49369088 |

第四次实际存活分配：原为38110602、Segment为38111460字节，几乎相同；私有提交则减少16084992字节，约15.34MiB/24.6%。第三次Segment样本曾升高，第四次回落，不能只挑最低值。该夹具保留首份程序身份JSON用于后续一致性比较，两组相同；绝对占用不能与正式服务横向比较。

因此产品manifest启用Windows SegmentHeap，内存/停止相关原生验证使用相同manifest。它是Windows 10 2004起支持的系统堆实现，微软说明通常可降低整体占用；本机A/B提供实际支持，但不等于已经完成50轮稳定性验证。依据：[微软manifest文档](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests#heaptype)。不额外加入HeapCompact“清理”：官方说明通常不会进一步压缩堆，见[HeapCompact说明](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapcompact)。

### 诊断开关与后续定位

- memory/info关闭时不再执行两次HeapSummary；空闲页回收保留。此前统计耗时从24ms升到344ms但即使关日志也执行，是实际无效开销。
- 启用时增加最多64条per_heap，记录主堆标记、地址、存活分配/内部提交/保留量、失败和耗时。仍写原四个固定槽，不建立新的无限历史；失败字节数null，不补0。
- 本轮没有分配栈能解释原27轮全部残余增长，RESOURCE_UNRESOLVED保留；不能将短期改善写成彻底无泄漏。后续用户允许循环时，按相同收尾边界比较per_heap和进程值，不重启全系统VA或重复已超时的CDB堆遍历。
- 历史整机提交压力最高99.01%，包含其它模拟器/应用；本轮未擅自关闭其它进程或修改系统分页配置。
- 部署后非暂停读取确认主堆Signature为0xddeeddee，!heap枚举为4个Segment Heap和1个NT Heap。但本机WinDbg 10.0.29617的!heap -s只输出辅助NT堆、!heap -i仅设置上下文，原采集脚本会据NT表头误报完整。本轮补上独立枚举和逐地址摘要覆盖检查；实际运行脚本明确返回HEAP_LAYOUT_HEAP_SUMMARIES_MISSING，回执记录4个缺失堆，未把失败冒充采集成功。该调试器的完整Segment统计能力仍不可用，后续全堆比较使用新增产品per_heap数据。证据：candidate129-heap-kind.log、candidate129-heap-inventory.log、candidate129-layout-coverage/receipt.json。诊断脚本为部署后修正的仓库工具，不影响已部署EXE身份，也未重新启动服务或游戏。

## 验证与交付证据

本地原始证据根：next/.local/giant128-50。replay-memory-v2.jsonl、ocr-real-rois.log、prepare-nt.json、prepare-segment-v2.json分别对应上述诊断。三张灰态原图、同屏亮态、暗屏和空字反例通过；原开启补点/变角色停止/手动防御不消费契约通过。最终停止验收见next/.local/stop-release-129-final/result.json；逐堆汇总/关闭采集门禁见next/.local/paused129-heap-boundary.log。

第一次事件回放使用错误的generation字段，退出失败；正确字段为session_generation，失败保留。第一次Segment副本位于.local，加载该目录旧ORT17，与所需API22不符，已只停止该隔离进程并拒绝其结果；有效v2使用独立目录和原锁定DLL。没有将这些失败当成通过。

candidate129已通过原manage_service部署到17654/原data，原PID36736正常退出，新PID32648、服务231488BA-8C24-4330-AD99-4D5E170EF228，创建时间134358461405668020。EXE SHA256为A0F5C96AD3334870EA488C91F33702D26E364382A663FF35B8458C60533DB3CB。实际嵌入manifest确认SegmentHeap，部署后Idle/busy=false/quiescent=true，repeat为空，未恢复循环，profile哈希未变。回执next/.local/paused129-deploy.log。无新commit/push授权，本轮不提交。
