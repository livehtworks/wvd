# M3 修复与完整性成本验证

日期：2026-09-14。工作包：WVD_MaaFramework_M3_Fix_and_M4_Pack_20260914。
实施起点：`4d7c4fabd29bfadf55963bfdaecf79e3b6582bdd`，初始工作区干净。
固定旧源：`6585f4075f5714ab522aa582993860c09af912c1`。

**整体结论：FIXES_INCOMPLETE。** 下面通过的是具体离线断言，不是完整工作包、实机或生产通过。
本轮没有 commit/push/PR、旧版打包、真实设备查询/连接/输入，停在 M5 前。
450 个冻结的生产文件、资源、旧配置和分发文件逐一核对无变化。

## 修改与边界

| 项目 | 已实现与证据 | 当前结论 |
| --- | --- | --- |
| FIX-ROI | 独立 CustomRecognitionScope，不再把 SDK roi 填入业务参数；默认、显式、排除及范围错误的 direct/Pipeline 对照 | 已测断言 PASS，真实质量未验 |
| FIX-CUSTOM | 统一请求解析；boolean-only 场景/后置允许 Hit；坐标目标仍须有位置；低置信目标不授权；后置需新帧 | 已测因果动作断言 PASS，完整业务未接入 |
| FIX-DISCOVERY | 固定 info 参数、自有 Job、限定继承句柄、双路重叠读、取消事件、绝对截止时间、2MiB 上限和清理 | FAIL/BLOCKED，见下文 |
| PERF-INTEGRITY | 私有物化快照、同对象 hash、共享只读锁、闭合成员、revision 索引、单次调用校验凭证 | 核心断言 PASS；完整性矩阵及资源成本未闭合 |

实现位置：`native/maafw/{gateway,recognition,preflight}.*`、`native/runtime/guarded_action.cpp`、
`native/games/wvd/vision/*`、`native/platform/windows/{metadata_query,bundle_lease}.*`、
`native/storage/runtime_bundle.*`。所有权和安全缺口详见 [快照契约](integrity-snapshot-contract.md)。

低置信 NEXT 结果新增 `action_eligible=false`，GuardedAction 与 InputGate 同时拒绝直接授权。
这不是自动战斗业务迁移，未实现的 WVD 流程不能据此被标为已修复。

## 最终产物回归

| 测试 | 结果 | 证明范围 |
| --- | --- | --- |
| M1 服务/盘点 | 23 PASS | 独立只读服务和静态索引仍可用 |
| 浏览器 | 4 PASS | 原工作台桌面/移动端回归；测试结束关闭自有服务 |
| M2 | 100 PASS | 原生识别、门禁、有限会话和异常收尾 |
| M3 原视觉组 | 8 PASS | 真实 SDK / 固定 OpenCV 的离线图像和受控后端，包括固定性能窗口 |
| 新增定点组 | 3 PASS | ROI 两种目标位置各 8 条 direct/Pipeline 对照；8 条因果动作路径；快照锁与 revision |
| M4 数据组 | 8 PASS | 33 字段、58 条目录及配置副本，不是任务执行 |

没有禁用既有测试。唯一改变旧断言的是 `test_resource_changed_after_load`：按指定快照契约，
尝试修改**活动快照**必须被锁拒绝，后续仍命中；新测试另证作者副本变化隔离和旧 hash 拒绝。
这不是把 Error 改成成功掩盖同一对象篡改，两个对象及其权威已明确区分。

M3 新增测试从本测试进程 GetModuleHandle/GetModuleFileName 获取实际 MaaFramework/OpenCV DLL 路径，
与锁定 SDK 目录/hash 对照；不是只检查下载缓存。原生日志、源/EXE hash 与结果保留在私有审核索引。
没有把离线 CausalDevice 的输入次数写成真实设备操作次数。

## 发现链阻断

自有 metadata helper 覆盖正常 JSON/stderr、无输出、持续输出、父退出而子进程持管道、非零退出、
超限、坏 JSON、取消及再次正常运行。读取错误、进程归属和清理都有记录。

首次查询的进程总句柄曾净增 5 个；去掉 UUID/COM 命名后未解决。
第二轮增加独立 CreateProcess 对照后，查询净增 2 个、对照净增 5 个，不能相减解释掉。
后续查询没有同样增量，但不足以证明首次所有对象正确回收，也不足以归因为系统缓存。
依照两轮修正上限保留 FAIL，不重跑挑 PASS。最终其他组回归没有再次执行这项已知失败。

另外，源码复核显示：MetadataQuery 的 `run()` 可以返回 CLEANUP_PENDING 并保留对象，
但 `mumu_binding` 当前把它放在局部变量中，离开作用域时析构仍等真实收尾。
因此当前绑定入口没有证明“先有界返回、由长寿命所有者继续持有未回收对象”的完整契约。
这是另一条明确的接续项，不能用 helper 正常退出或外部 watchdog 代替。

恢复此项需要先明确并实测句柄来源，并将查询所有权与 pending 状态完整接入调用者；
不得通过 detach、释放未完成 OVERLAPPED、放宽控制者检查或杀未知进程通过验收。

## 固定性能窗口

修前与修后各执行一次小包/全包测试，每组 5 次预热、30 次正式。
同一 900x1600 合成帧、相同参数、模板字节和文件清单均逐一比对一致。
全包含 437 个原资源成员及 4 个测试成员；不能称为 441 个真实游戏场景。

| 包 | 修前中位 / p95 | 修后中位 / p95 | 初始化前 → 后 |
| --- | --- | --- | --- |
| 小包 4 文件 | 32.91 / 39.52ms | 24.49 / 29.27ms | 65.51 → 122.58ms |
| 全包 441 文件 | 562.00 / 658.85ms | 36.81 / 48.56ms | 488.02 → 1224.03ms |

全包完整性检查阶段中位从 266.63ms 降为 12.54ms；
包含 Custom 回调的原生阶段从 272.04ms 降为 1.85ms。目录闭合扫描仍然存在，不是零成本。
修后每次 direct→Custom 只有一次成员校验，35 次热调用的完整文件 hash 增量为 0。
全包活动 lease 持有 441 个文件、23 个目录句柄，缓存原字节 11,939,799 bytes；关闭后 active=false。
源复制期还有临时源 lease，初始化期间峰值不能用活动期计数代替。

这些是**离线识别耗时**，不包含实机截图传输，也不是游戏完整任务速度。
没有补测峰值私有内存、完整工作集和原生分配归因；修前 EXE 独立封存证据也不如修后完整。
故本项整体仍为 PERFORMANCE_UNRESOLVED，不能只凭中位数宣布资源/长期性能放行。
不重跑 M0，不把旧 RESOURCE_UNRESOLVED 改成 PASS。

## 证据与接续

私有工作根：`next/.local/m3fix-m4-9c6409e289f74725932570702eeaee04/`。
最终定点：`next/.local/m3-fixes-q24ace8j/`；旧失败：`next/.local/m3-fixes-gy4w3z_r/`。
修前成本：`next/.local/m3-vision-rc_iy1qn/`；修后：`next/.local/m3-vision-aofera3z/`。
M2：`recognition-pekeq0uh` 与 `runtime-3d8an5s8`，位于 `.local/m2-runs/`。
工作根中的 `performance-summary.json` 为原数据重算，所有首次失败和修正日志均保留。

下一步先处理上述明确阻断并补完整性负例矩阵；不依赖设备的 M4 状态/计划可继续建设，
但不得开放不完整业务运行或生产替换。M4 范围详见 [业务报告](m4-business-validation.md)。
