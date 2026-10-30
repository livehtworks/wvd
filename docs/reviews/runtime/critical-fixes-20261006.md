# critical-fixes 工作包：Windows 交付与未闭合项

## 结论

本轮完成 C01–C03 源码接入、Windows 定向验收及 candidate121 部署。W01 完成；W02 分配栈实验已执行但失败，不足以归因；W03 取证调用链完成，历史八次技能失败的真实原因仍未取得；W04 完成分析及单一建议点，未改战斗图；W05 完成所有权/取消审查；W06 完成同窗口时间对齐，设备故障与内存的因果仍未证明。**不能宣称全部缺陷关闭或内存稳定通过。**

当前正式服务 `http://127.0.0.1:17654/` 为 candidate121，PID38436，实例 `6DD72FBD-D995-4900-9370-19EECB401B7B`，创建 FILETIME `134357701182075891`；Idle/quiescent、run_id=0，没有循环运行。数据仍为 `next/.local/c11-flow-product/data`。用户授权最多两轮、20分钟、跟踪文件合计512MiB，不授权扩展采集。两轮请求 `6ac382d1-214b-4b2a-b11c-6d1425f2eaf9` 在准备阶段取消，错误 `PREPARATION_CANCELLED`；**实际新轮数为零，不把准备或部署计作游戏成功。** 初次交付未获 commit/push 指令，后续用户明确授权提交源码和报告到个人 `fork`；用户 `.vscode/` 不纳入，候选历史身份不回写。

## 基线与实施

- 基线 `c2b5bee1fca79ef35a05eb7193a36dfffa93626b`；ZIP SHA256 `E1409A0CC5A4A8418D44BDD5982CDF2EF687AEE470685ECD40ABF3F6C699D8F6`。
- 42个归档文件校验、14个补丁源码基线核对及应用后核对通过。先在独立 Git 工作树 `S:/codex_workspace/codex-home/worktrees/critical-fixes-20261006/wvd` 构建，再按差异接回主仓库，未整目录覆盖。
- 应用管理工具创建 checkout 后，扫描主仓库 ignored `AGENTS.override.md` 停滞；仅结束本次只读扫描进程，注册最终失败。实际 checkout 已核对归属与基线，但两次 attach 未获归属验证，因此不声称已在应用中附着；工作树保留。
- 未升级锁定依赖、未改 ROI/阈值/重试/技能消费/Auto/付款/循环语义，未调整 ORT/OpenCV 分配器或线程配置。未写用户 profile、公共流程及旧 Python 产品。
- 正式 profile SHA256 前后均为 `3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C`。

## C01–C03

**C01 启动/再起交接**：非普通启动内部、外部重启确认及设备重启恢复共用 `(boot_ready 或 RiseAgain) 且非阻断页且非普通剧情页`。再起交给原业务复活处理，不把启动就绪当旧 pending 的后置成功，不新增复活输入。

**C02 模型原缓冲**：ONNX 从已锁定原 HANDLE 以64KiB块计算 SHA256，不在创建租约时常驻完整模型 vector；路径、文件锁、重解析点拒绝、身份/哈希及256MiB上限仍有效。需要 `bytes()` 时，同一 HANDLE 在互斥域内校验并惰性发布不可变缓存。这里只移除模型原字节副本机制，不声称移除全部135.53MiB，也不把约93.82MiB模型成本等同历史45.11MiB收尾增长。

**C03 永久战斗取证**：正式协调器 input/event/capture 回调关联 `combat-open-detail/open` 的已知送达 `no_progress`，保存该失败原帧；随后真实防御确认保存第二张原帧，同 operation_id。prepare 回执记录配置技能。既有 RunStore 唯一写入链、失败额度32张/单帧8MiB继续有效；两张各占一次，完整配对最多16组且与其他失败共享额度。context最多16KiB、选择元数据最多4KiB；operation-scoped 去重不误吞另一独立操作。缺图/错身份/跨连接/写盘失败明确不完整，不触发额外截图、线程或历史帧持有，不把防御当施放成功。

## W01：Windows 验收

保持原 CMake/编译器/依赖锁。配置 `cmake --preset windows-x64 -S next`，定向构建 `cmake --build next/build --config Release --target test_native_author test_native_coordinator test_native_recognition test_native_flow automationd -- /m:4`。匹配符号候选采用既有 Release `/O2 /DNDEBUG /Zi` 和链接 `/DEBUG`，不改源码 preset。

| 既有目标与选择器 | 最终结果 | 本机日志（相对证据根） |
| --- | --- | --- |
| recognition `--bundle-lease` | PASS | `validation/` |
| coordinator `--combat-diagnostic` | PASS，方法级 | `validation/` |
| author `--boot-revival` | PASS，13场景 | `validation-repair/` |
| author `--boot-progress` | PASS | `validation-repair/` |
| author `--combat-open-repair` | PASS | `validation-repair/` |
| author `--critical-input-protection` | PASS，32保护计数及5类直接断言 | `validation-protection-final/` |
| flow `--critical-recovery` | PASS | `validation-protection-final/` |
| flow `--confirmed-result` | PASS | `validation-protection-final/` |
| flow `--restart-during-boot` | PASS | `validation-protection-final/` |
| flow `--exception-restart` | PASS | `validation-protection-final/` |
| author `--combat-diagnostic-drive` | PASS，正式协调器整链 | `validation-drive-final/drive.log` |
| author `--memory-owners <candidate121/pack> <历史900x1600原帧>` | PASS，真实繁中OCR加载/推理/释放 | `validation-ocr-streamed-final/ocr.log` |

以上最终命令退出码均0，不运行无关全量测试或游戏长循环。真实OCR检查只读候选资源并复制到隔离临时包：总文件113,494,010字节，其中流式模型98,380,172字节；OCR前后原文件常驻15,113,838字节、模型原缓存0，释放后租约弱引用失效及Service/OCR存活断言成立。OCR结果要求非Error（原帧不保证HP文本命中），不冒充识别成功正例或泄漏归因。复用旧所有者检查，更新旧“全部模型必须常驻”断言为当前保留/哈希契约，未删除释放断言。此测试头在产品候选冻结后修正；不重建/重部署产品、不回写121清单。

新增整链夹具使用正式编译器、FlowExecutor、`NativeRunCoordinator::start/drive`、NativeFlowPorts、WVD NativeOperations/RunState、Service及RunStore；只隔离离线像素/底层送达和视觉叶子，不通过 friend 调诊断方法、不手工注入 no_progress 或业务 Completed。真实执行开技能补试耗尽同页窗口，再执行 WVD 防御确认，核对两张原始PNG、operation_id/帧/epoch/连接及永久事件，最终保留未施放技能行。日志关闭及性能关闭下仍保留必需证据。**不证明实机按钮可用、SP足够或视觉叶子准确。**

构建和夹具遇到的失败原日志未删：MSVC JSON字符串比较需显式 `get<std::string>()`；恢复夹具须推进真实代次并安装原 PublicStepScope；保护夹具补现行任务配置及六角色目标；离线 Backend 提供有效 rotation。所有者检查显式引用BundleLease定义，author命令在`next/`工作目录读取原资源；初次漏include及错误工作目录日志也保留。修正的是夹具/平台编译错误，未删保护断言、未加测试专用生产分支。英文字典锁要求原CRLF字节，使用已核验权威字节副本，未改资源哈希锁。

前端来自独立源码及原锁定依赖：Node24.14.1，`vue-tsc --noEmit`、`vite build --configLoader native`通过；没有 UI 功能变化。candidate120仅为无PDB暂存候选，未部署；实际部署candidate121。候选清单保留基线提交加 dirty source 哈希，不冒充已提交构建，后续仅测试/文档变更不回写历史身份。

| 身份 | EXE SHA256 | PDB GUID / Age |
| --- | --- | --- |
| candidate121 | `93B9D3248DFBD0CF4ED1732FC6461546A94345B212F43AED0217922084D28D80` | `ed9393fc-b7c4-4bc1-a349-b827125cb4f3` / 1 |
| 原candidate119 | `9EE2E139968647200FFDA649C17D0DC5576F95BF70E9E25FDA636C55287C88A4` | `f75103ce-903b-4757-b8f3-dd20d5e78eac` / 73 |

121 PDB SHA256 `35BFB5A2A80305B40BB237EBB2989D92667CD3E92B7C0A10E7A65685A789690C`。119原始匹配EXE/PDB只读保存。对应关系由 DbgHelp `SymSrvGetFileIndexInfoW` 核对，不是重新编译同名PDB替代；**文件匹配不等于WPA实际加载符号成功。** 部署使用正式管理入口，旧119退出后121在原端口/数据启动，没有旁路后台。

## W02：分配栈采集失败，RESOURCE_UNRESOLVED

采用指定 WPR Heap + VirtualAllocation 文件模式，未并行换UMDH。WPR10.0.26100，WPA exporter11.7.383.39833。开始前无其他录制、automationd堆配置关闭；记录IFEO原键/值均不存在。独立会话 `WvdCritical_6ac382d1214b4b2ab11c6d1425f2eaf9`，仅配置 automationd 堆，配置后冷启动121；内核VirtualAlloc提供者本身并非目标PID过滤，不能说ETL全为目标进程。

| 实际时点（UTC）/指标 | 记录 |
| --- | --- |
| ready | 2026-10-06 14:20:15 |
| 临时容量保护触发 | 14:21:57，约102.84秒 |
| stop/合并完成 | 14:23:24，约189.96秒 |
| 最终ETL | 458,227,712字节，437MiB |
| 丢失事件 | **136,897** |
| 新轮、worker_joined边界 | **0轮，无边界** |

filemode不自带硬容量限制；脚本以实际打开文件长度检查，临时128MiB阈值预留stop合并空间，总上限512MiB。本次ETL最终在上限内，但合并耗时约87秒、事件丢失且必要边界未覆盖。两轮请求尚在准备时取消；未继续补轮、增加容量或重开采集窗口。

xperf统计导出退出 `-2147023504`；WPA exporter拒绝继续处理丢事件ETL，退出 `-2147008507`，明确 `Lost events detected, and continuation was denied`。未忽略丢失警告、未输出可信分配差分；符号文件匹配已核对，但本次**实际符号加载及调用栈归因 NOT_VERIFIED**。Heap活动块/页留存、ORT/OpenCV/CRT所有者份额及未覆盖余额均 unknown。原始ETL、receipt和失败日志保留，不修改 receipt 伪装成功。

结束核对本会话已关闭、WPR无录制、automationd堆配置关闭、IFEO原状态恢复。**45.1133MiB历史收尾增量仍未归因；C02不关闭此项。** 后续需要重新批准更低事件吞吐且仍有界的WPR方案，先解决本次容量/丢失条件，不能靠继续长循环收曲线替代分配栈。

## W03：真实技能失败原因尚未取得

取证正式回调链及永久落盘已验收。有限实机窗口在准备阶段结束，未自然产生新的技能失败，也没有拿到历史八次配对原帧；SP/MP、等级可用性、按钮失效、弹窗具体原因保持 unknown。未制造死亡/异常，未用当前页面补记历史失败，未改技能或保底行为。下一自然故障发生时按 operation_id 查永久证据即可，不必扩大普通成功截图频率。

## W04：Actor_Entry 分析及唯一建议点

读取119成功34轮、947段 Actor_Entry；平均节点墙钟95.42秒。其自身 exclusive 每轮：match41.595秒、parallel_wait21.469秒、capture_metadata16.737秒、recognition_other9.306秒、explicit_wait4.630秒、pixel_capture1.277秒、pixel_convert0.112秒；各独占桶之间分列，不与墙钟或worker相加。约4413次实际匹配/93.59次截图每轮。

唯一建议点是 `take_turn()` 的 Entry 在已知战斗、尚无可行动菜单的窗口复用现有声明的战斗ongoing/progress观察覆盖，减少重复结束/Auto/倍速/prepare扫描。当前历史记录缺逐候选 Hit/NoHit、frame_id/action_epoch 对应，不能把所有947段都认定动画；因此**仅提交建议，不改turn.cpp**。不改any/all错误语义，不缩ROI/降阈值/扩大TTL，不承诺95秒全部可回收。

## W05：OCR 生命周期审查

巨人编译 `task_unit_count=3`，历史三段 checkpoint 是同一业务程序的三次 unit/generation，不擅自命名启动/战斗/返城三个阶段。每unit创建 Service/ExecutionSession，共享不可变程序与资源租约；Service仅初始化当前语言，模型槽有界，识别缓存受原额度约束，销毁释放 OCR/ORT所有者。

Service.cancel 是 sticky atomic；DbNet/CrnnNet 的 RunOptions.SetTerminate 没有复用重置协议，取消后的模型不能直接借全局缓存跨Session再用。OcrEngine推理互斥，DbNet/CrnnNet拥有OrtSession；现有CPU intra2/inter1/SEQUENTIAL及spinning策略均未改变。初始化平均512.598ms，三次约1.538秒/轮，不是主要速度瓶颈。**未实现跨Session/全局OCR复用，资源增长归因仍未知。**

## W06：run34同窗口设备故障

通过 EventJournal 与带UTC的 action-timing 中105条相同 input.result 锚点对齐，offset spread约1.065ms；不是将单调时钟猜作UTC。

| 北京时间2026-10-06 | run34实际事件 |
| --- | --- |
| 18:11:03.904 | seq1579，`capture:dumpsys window` ADB_TIMEOUT，预算5000ms/实耗5028ms |
| 18:11:11.000 | seq1581，screencap ADB_TRANSPORT_FAILED，预算8000ms/实耗110ms，exit=-1，stderr `device offline` |
| 18:11:11.830 | 绑定实例恢复要求 |
| 18:11:46.340 | 应用重启/前台恢复/重连回执均true |
| 18:11:58.172 | 连续异常期限触发绑定游戏重启 |
| 18:12:05.912 | 应用/前台恢复true，重连false |

首故障前14.613秒同轮压力：系统提交96.3925%，可用物理内存3.7179GiB；该采样不是精确故障瞬间。不能用另批run5的99.25%峰值解释run34。已知ADB offline，不等于证明内存导致模拟器退出。缺父读取期限剩余值、故障前模拟器PID/创建身份；恢复布尔值不能替代OS身份。保留因果未确认，未削弱设备门禁或未知送达保护。

## 证据索引与剩余工作

证据根：`D:/programcode/python/wvd/next/.local/work-packages/critical-20261006/`（本机ignored，不将437MiB ETL提交Git）。`verify-*.log`、`build-*.log`、`prepare-*.log`保留初次失败与最终修正；`symbol119.json`/`symbol121.json`、`trace-plan.json`、`trace-*/trace-receipt.json`/`trace-stop.log`、`remaining-analysis.json`、`adb34-aligned.json`分别对应上述结论。整链PNG在 `validation-drive-final/combat-drive/drive/340619E8-9DCF-4145-B2DD-A1E0610D35FC/1/`。

仍开放：有效分配栈差分（W02）、自然技能失败原因（W03）、Entry动画窗口覆盖和优化审批（W04）、设备故障/内存因果的缺失身份字段（W06）。不因离线全部通过放行长循环，也不自动扩大这次两轮授权。
