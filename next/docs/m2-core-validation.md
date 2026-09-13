# M2 运行核心与离线验收

日期：2026-09-13。承接用户“继续推进 M2 剩余内容”的要求。

## 阶段结论

**M2 限定离线核心完成。** 最终固定产物上的 85 项 M2 测试（29 项识别、56 项运行/门禁/存储）
全部通过，耗时 59.025 秒；23 项 M1 服务/盘点回归通过；4 项桌面/移动端浏览器场景通过。
这不是生产放行：真实后端仍在 connect 前拒绝，旧版仍是唯一有效生产入口。
M1 修复单独封存；本轮没有重新计算 M0 内存结果，没有启动 M3、操作模拟器或切换生产。

### 最终证据

- 完整构建：`next/.local/m2-final-build.log`；最后两处生命周期修正后全原生目标重建：`next/.local/m2-final-native-rebuild-2.log`，均退出 0。
- 唯一最终验收轮次：`next/.local/m2-final-validation-3.log`，退出 0；上述日志及详细测试输出已独立封存在 `next/.local/m2-core-final-20260913/`。
- 识别结果：`next/.local/m2-runs/recognition-muxw7pkj/results.json`；运行结果：`next/.local/m2-runs/runtime-5tmd13iw/results.json`。
- 每个用例目录保存合成帧、真实资源清单、原生结果、stdout/stderr、独立 run-data；不提交这些可能含私人路径的原始证据。
- 450 个受保护文件 SHA256 与本轮前的基线一致，覆盖旧源码、运行配置和打包入口；没有改写 M0 历史结论或清理用户日志。

| 最终文件 | SHA256 |
| --- | --- |
| test_runtime.exe | A798B4405BED2078D0E029646F31DB46637A883640FAB98FBBFAC264D4742DAB |
| test_recognition.exe | 88A86A73A47E170DE47534BB0CCD02233E53BF38B117A42E7F4076C1EF3E3D65 |
| runtime-5tmd13iw/results.json | C2CCA327E9DCF129FC291B131FDB87E4143597B8B6EE2C5F615ED6370FE083BE |
| recognition-muxw7pkj/results.json | 86AABDE62C18D754BF3B2AD1254B965747306B34818A6E2AE08EEEF2BFFB86F4 |

## 已实现的完整链

| 层 | 本轮实际职责与入口 |
| --- | --- |
| contracts | FrameIdentity/Observation、Command/ActionIntent/InputPolicy、SessionResult/RunSnapshot；不持有 SDK 指针 |
| runtime/RunCoordinator | 请求去重、冻结定义、独占设备、监督停止、有限恢复、应用终态与结果提交 |
| runtime/ExecutionSession | 每代独立工作线程、SDK 生命周期、失败/停止/恢复状态、真实根终点证据；结束后禁止复用 |
| runtime/GuardedAction | 新帧场景/目标识别、单次意图、受控输入、新帧后置确认；不盲目重试有副作用操作 |
| maafw/MaaGateway | 唯一 SDK 适配与所有者，真实 Pipeline、Context/clone、子任务详情、回调与正常析构 |
| maafw/recognition + preflight | 模板/OCR 统一 Hit/NoHit/Error；完整清单/hash、参数、图像与模型预检 |
| maafw/GuardedController | 覆盖 SDK 全部输入回调，包括内置动作产生的隐式输入，异常不跨 C ABI |
| devices/InputGate | 真实帧身份、代次、时效、epoch、viewport、场景、应用、权限、区域与停止准入；原始坐标只映射一次 |
| platform/windows | 有名称的设备信号量、系统 GUID、同目录原子写入；不执行 shell 或模拟器操作 |
| storage | 有界事件、运行元数据、根证据与终态事件一次性原子提交；未提交历史只读为 Interrupted |

`OfflineRecognizer` 和 `ExecutionSession` 共用同一 MaaGateway，原识别独立库已并入 `wvd_core`，
没有第二套节点解释器、双控制者或隐藏回退链。M1 服务仍未链接核心，现有只读接口不变。
核心和业务分层详见 [架构](architecture.md)，持久化职责详见 [数据权威](data-authority.md)。

## 验收断言与测试映射

所有运行/识别使用固定 MaaFramework v5.13.0 的实际 C ABI、真实资源和 TaskDetail。
离线设备只替代本阶段不允许操作的游戏/ADB；不替代 Maa 识别、调度、回调或停止链。

| 要求 | 本轮实际验证 |
| --- | --- |
| 三态不能把错误当 NoHit | 原有 29 项真实模板/OCR 正反例、坏资源、帧/参数/身份预检全部保留 |
| 重复 start 不双跑 | duplicate-start、idempotency-conflict、old-request-replay；连接和底层输入数量严格核对 |
| 会话不能复用旧状态 | sequential-reset、resource-isolation、session-restart-refused、gate-reinitialize |
| 子任务有效 ID 不等于成功 | child-failure：TestFalse 仅返回 false；真实 TaskDetail 为失败，父级 CHILD_FAILED，后续底层输入为零 |
| 根业务结果独立 | root-not-terminal 引擎 Succeeded 仍失败；wrong-root-node 不接受其他节点终点；恢复使用新 generation |
| 实际进入嵌套流程后停止 | nested-stop、clone-stop 等到真实 ChildWait/depth=1 的回调再请求停止 |
| 内置动作也不能绕门禁 | builtin-unguarded、gate-bare-all 对全部 15 个回调入口逐项核对，未授权 backend_called=0 |
| 旧观察不能重新获准 | gate-generation / viewport 从前一真实 Maa 识别获得 Observation，在新会话/viewport 提交并拒绝 |
| 新帧与动作 epoch | gate-epoch、expired、late、bounds、permission、package、nohit、error |
| 输入与后置确认不能混同 | normal、postcondition-timeout；输入成功但后置未满足仍失败，且不重复输入 |
| 坐标与 SDK 动作语义 | large-frame 的 1080x1920 截图在基准空间识别，只映射一次；真实 Click/Swipe/Scroll 路径 |
| 停止必须真正静止 | wait/custom/nested/clone-stop；held-touch/key 清理一次，正常对象销毁后才 UserStopped |
| 原生阻塞不能伪报停止 | stop-timeout / stop-during-connect / release-timeout：请求迅速返回，超时 Failed 且 quiescent=false，保留租约并拒绝新运行 |
| 独占设备跨进程成立 | cross-process-lease：独立进程持有时探测 busy，正常退出后才 acquired |
| 恢复不能混入旧会话 | recovery 使用两个真实 SDK Session；unresolved-recovery 只 Interrupted，不擅自重开游戏 |
| Pipeline 原生语义 | clone-isolation 检查父节点数据不变；interrupt 使用真实 JumpBack 与有限 max_hit |
| 终态一次且确实落盘 | result-save-failure、store-once、store-interrupted、store-terminal-transaction；失败不发布 run.terminal，不覆盖旧结果 |
| 事件有界且可补齐 | store-events、critical-overflow，检查事件字段、序号缺口、关键事件保留及独立终态槽 |
| 真设备入口仍关闭 | real-device-rejected：在 connect 前拒绝，连接与输入均为零 |

## 调试中确认并修正的问题

1. 非坐标动作原先也填入目标中心，导致 KeyDown 的实际回调参数与意图不符。现在只给位置动作填中心。
2. 子 Custom 返回 false 时泛化失败抢先覆盖子任务原因。现在子调用由真实 TaskDetail 统一报告 CHILD_FAILED，根回调继续保留独立失败处理。
3. SDK Scroll 先调用 TouchMove，随后才 Scroll；Inactive 也会由 SDK 自动触发。附加输入逐项观察和拒绝，不靠缩小计数掩盖；滚动许可只允许真正的 Scroll。
4. 终态不能在写盘之前暴露。现在预构造结果及终态事件，原子提交成功后再发布；关键事件已满也有一个独立终态槽。
5. join 后线程不再 joinable，原检查可能误允许同一 Session 再 start。增加不可复用标记；Gateway 同样只允许初始化一次。收尾测试还抓到该标记一度误放到析构检查，造成独立会话用例退出异常；已修正为只在 start 消耗标记，析构仍依据 joinable 正常回收。这是本轮代码问题，不归因于 Maa。
6. 坐标测试原样本边缘与随机背景在双重缩放后混合。增加模板外围平滑边缘，保留 0.99 阈值和真实坐标断言，不改任何 WVD 识别算法。
7. 过期测试的短时效偶尔在场景准备阶段就耗尽。改为按实际采集时刻等待明确过期后再提交，继续断言 backend_called=0。

完整失败轮次保留在 `next/.local`。其中一次构建未结束便启动测试导致 exe 占用，
该混合轮次明确不作验收依据；最终必须先完成构建再启动回归，测试同时核对 exe 哈希。

## 停止与稳定性边界

- 正常执行使用一个协调监督线程和一个会话工作线程，SDK 自身线程由其实际对象生命周期管理；没有新增应用线程池、detach 或强杀兜底。
- STOP_TIMEOUT 后不释放设备，也不谎报 UserStopped。若底层永不返回，仍须保留占用；这证明故障可观察和输入关闭，不证明任意原生调用可取消。
- 只在旧会话真静止后决定有限恢复。该纯决策不操作设备，M3/M4 再承接具体游戏恢复链。
- 每实例去重历史最多 256 个请求，事件常规容量默认 256 条、每条载荷最多 64 KiB，终态另留一槽；达到关键容量显式拒绝，不静默覆盖。
- 活动事件快照和终态提交均使用新的独立运行目录；没有给正式配置、mod、旧日志增加读写者。
- M0 的 `RESOURCE_UNRESOLVED` 保留。本轮没有拿对象析构计数代替资源归因，也没有重跑 285 组内存诊断来挑选 PASS。
- 真实 NEXT/Pause、MuMu/ADB 适配、设备场景/VPN、WVD 全任务、Spark 仍未验。85 项离线测试不是 85 轮游戏挂机。
- 配置导入/编辑、HTTP 运行接口、WS、生产日志保留策略和旧版替换仍属后续阶段，没有提前接入真实入口。

## 复现

准备固定 SDK/模型后，仓库根目录执行：

```powershell
.venv-build/Scripts/python.exe next/tools/build.py --m2-offline
.venv-build/Scripts/python.exe next/tools/validate.py --m2-offline
```

必须先确认构建退出码为零，再执行第二条。工具不会连接 MuMu、执行 Farm 或读取正式 config.json。
Windows 的正常停止与后台测试进程规则见根项目 [执行注意项](../../docs/execution-notes.md)。

本次验收阶段未 commit/push，未打包旧 wvd，未清理用户日志。
验收完成后用户另行授权提交并推送个人 fork；M1 收尾与 M2 核心分开提交，结果以 Git 历史为准。
