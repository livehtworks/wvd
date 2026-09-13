# M2 收尾与 M3 设备视觉验收

日期：2026-09-13。工作包：`WVD_MaaFramework_M2_Fix_and_M3_Pack_20260913`。

## 阶段结论

- Gate A：**PASS**，M2-R01/R02/R03 修正及原覆盖保留，详见 [M2 修正报告](m2-fix-validation.md)。
- M3：**M3_IMPLEMENTED_WITH_GAPS**。有限设备截图、资源和纯视觉实现已完成；系统导航仍为安全拒绝入口，真实 NEXT/Pause 质量与部分实机异常条件未闭合，不能将其算成全范围验收通过。
- 最终产物回归：100 项 M2、23 项 M1、8 组 M3、4 个浏览器场景通过；另有 7 项真实本地入口绑定拒绝检查、437 张资源解码及 112 张设备样本核对通过。组数不冒充断言数或游戏周目数。
- 旧生产保护清单 450 个文件前后 hash 相同，未修改旧源码/资源、配置、mod、打包入口或旧 EXE，未清理用户日志。
- 未 commit、push、打包旧版或切换生产；**M4 未启动**。`RESOURCE_UNRESOLVED` 保留，不重跑 M0。

## 基线与依赖

起点 HEAD `4932207adc6565ccbac43c4414061eac95712dd5`，分支 `agent/local-stability-notes`，初始工作区干净。
旧算法/资源来自 `6585f4075f5714ab522aa582993860c09af912c1`，原静态盘点没有重写成“已迁移”。
本轮实际执行者为当前 Codex；未切换模型或订阅，运行环境未向工具提供可核实的推理档位，故不编造该字段。

| 依赖 | 固定来源与核对 |
| --- | --- |
| MaaFramework | v5.13.0 / `2bcfa85c66a2eac6ca3e5937f175495275ee0643`；原生结果回报 v5.13.0 |
| SDK 归档 SHA256 | `179b51b6211fc74317c8dc06f10fae4724aca370df7d1bf694cd00d06ff38efe` |
| MaaFramework.dll 文件 SHA256 | `d4503d525561a7be46d7b2a5e64f3288f5b3ba7aaefaf36ab44f7ce4c04cffae` |
| OpenCV | 4.12.0，官方 MaaDeps v2.12.6 x64 Windows devel；归档 SHA256 `f67031e183735fad3faa6f9280f509d3ff1e25ae0e7e33a9185f275ef0f79cf7` |
| OpenCV DLL | SDK 与 MaaDeps 的 `opencv_world4_maa.dll` 同为 `9912a1f359b3a94e30d738aa5ed02cdfa3d805405f88432c26bea34b16cf6f8e` |
| 其余依赖 | 延续 `dependencies.lock.json`、英文 OCR 模型锁及 Web package-lock；不升级 |

CMake 校验配置指向的头/库/DLL 文件，运行通过 SDK PATH 载入并验证版本；本轮没有额外采集进程模块加载路径清单，不将文件 hash 说成独立模块取证。

## 实现与证据

| 子项 / 验收 ID | 已完成 | 验收边界 |
| --- | --- | --- |
| M3.1 / D-DEFAULT-DENY | M1 不链接 Maa，默认离线真实连接拒绝保留 | 原 M2 `real-device-rejected` 继续通过 |
| D-BIND / D-LEASE | 配置线索、单实例创建标识、安装根、ADB 路径/端口与控制者核对；Local 租约不变 | 正确绑定实测；schema、归属声明、非法编号、序列、创建标识、ADB 路径和安装根 7 项拒绝实测；租约争用沿用真实 Windows 离线回归 |
| D-IPC / D-ADB / D-FALLBACK | 唯一内层原始 Controller，IPC 优先、Encode 后备；先销毁再重建 | 两种实际后端分别 50/50；主失败/捕获失败/双失败/不每帧重试由边界注入验证，不冒充真实 DLL 故障 |
| D-RECONNECT | 同一 IPC 后端对象在会话静止后重建；连接代次 1→2 | 一次正常重建成功，后置两帧成功；旧观察拒绝由门禁离线断言支持，实机未尝试任何迟到输入 |
| M3.2 / V-VIEWPORT | WVD 固定比例映射；另设实测系统只读 viewport | 五种游戏比例离线通过；1600×900 系统横屏实测，不代表实际 WVD 900×1600 已测 |
| V-FOREGROUND / I-REJECT | 真前台观测、输入前重核、失效帧撤销；解码错误也撤销当前帧 | 前台变化、过期/错代次、捕获/解码失败、系统只读禁止输入离线通过；设备端原子确认与点击仍无保证 |
| M3.3 / A-EXACT / A-ALIAS | 437 PNG 原字节复制，8 处引用映射为 7 个唯一别名，44 处动态引用逐项保留 | 全量 hash/解码通过；不改旧文件名、不转小写、不重编码 |
| A-PRECEDENCE / A-ERROR | 基线优先、显式别名、隔离 mod 后备；按规范路径/revision 隔离缓存 | 同名基线优先、mod 补缺、跨包同名、缺图/坏图/路径/完整性错误覆盖；未导入用户 mod |
| M3.4 / V-BASE / V-ROI / V-MASK | 普通相关、亮色遮罩、多 ROI/排除、成组候选、通道预处理 | 旧 AST `_check`、亮度遮罩及真实 CutRoI 分支对照；通道预处理逐像素对照；原帧未修改 |
| V-BATTLE / V-MAP | Active 四变体、角色裁剪、技能等级、地图光标/到达/楼梯等纯部分 | 合成埋点和负例经真实 Gateway；混合函数只迁纯判断，技能动作/地图导航/策略消费未迁移 |
| V-NEXT-LOGIC | 普通 NEXT 优先、三角标记后备；低置信 0.60 独立模式；显式尺度参数 | 0.86 默认不降低，不默认改为遮罩；低置信结果不自动授权点击；真实多尺度/边缘/遮挡质量 UNVERIFIED |
| V-PAUSE-LOGIC | 暗底/白字候选、角色/技能反证、连通域布局 | 合成布局正例与负分支通过；没有声称完整真实 Pause 回归通过 |
| V-PAUSE-OCR | 保留“旧 Tesseract 不可用”的显式 Error | 未安装或悄悄替换为 Maa OCR；旧 OCR 等价性 UNVERIFIED |
| 其他纯视觉 | 浮标方向场、去重和模板过滤 | 旧 AST 同输入对照通过；不迁入钓鱼点击和循环 |
| M3.5 / I-SAFE-SYSTEM | 入口明确拒绝未确认的系统导航 | **BLOCKED**：尚未固化特定系统页面的前后场景与许可适配，不把桌面截图有效当成点击许可；没有实际进入/返回测试 |
| L-STOP / L-RELEASE | 同一协调器、Session、门禁、原子终态与释放路径 | 原生离线停止/超时/释放回归通过，实机正常结束；实机强制阻塞取消未验证，不能把正常退出称为任意等待可取消 |
| M3.6 / P-FULL-BUNDLE | 完整视觉包分段成本、固定样本数、失败记录 | 数值见下表；没有删除完整性检查，没有性能/资源生产放行 |

职责文件：`native/runtime/behavior_registry.*`、`run_coordinator.*` 负责有效定义及终态；
`native/maafw/adb_backend.*`、`gateway.*` 负责 SDK 对象和 C ABI；`native/devices/input_gate.*`
负责最终准入与坐标；`native/platform/windows/mumu_binding.*` 负责实例证据；
`native/games/wvd/vision/` 只做纯计算；`native/app/m3_check.cpp` 只组装有限只读检查。
完整 28 项源符号去向见 [实现叠加表](migration/m3-implementation-map.json)。

## 现场与失败轮次

目标按旧配置线索与 MuMu 管理器元数据交叉定位，没有遍历启动其他实例。初始目标关闭；本轮只启动该既有实例一次，前台最终是 Android launcher，不是游戏。
没有打开 Clash/VPN、游戏、设置、任务或消费界面；没有修改分辨率/DPI。检查执行的 shell 仅为 SDK 连接、设备/包/前台只读查询及截图所需命令，真实输入计数为零。

1. 首次设备检查被 `PYTHON_CONTROLLER_OWNERSHIP_UNCONFIRMED` 阻断，尚未创建 ADB Controller。该临时进程随后消失，未记录到足够身份信息，**原因未归因**；没有放宽或删除控制者检查。
2. 再次核对无其他控制者后连接成功，但首张预热图因 `FOREGROUND_UNCONFIRMED` 结束。运行是 Failed、quiescent=true、result_saved=true、输入为零。只读对照确认 Android 15 的 `dumpsys window windows` 缺焦点，而 `dumpsys window` 包含焦点，修正此查询。
3. 修正后重新构建并完成最终全量回归；随后有限实测 IPC/Encode/正常重建全部成功。前次预热失败单独保留，不从所有尝试历史中抹去；正式 50 次分母属于这次明确的修正后运行。

该元数据错误曾触发主→后备路径，**不是 IPC DLL 自然故障证据**。固定 SDK 在捕获失败内部会重试并执行 KillServer；本实现用其公开 `command.KillServer` 配置替换为目标设备的只读 get-state。
源码依据为固定 SDK 的 `AdbControlUnitMgr.cpp`、`General/Connection.cpp` 和 `Base/UnitBase.cpp`。没有修改 Maa 核心。
SDK 内部重试仍存在，且其内部重连不完全暴露给外部连接代次；这一限制保留，真实动作不因此开放。

结束时三个真实有限运行均为 Completed / quiescent=true / result_saved=true，backend_called=0。
检查程序和测试进程正常退出，未强杀。全局 ADB daemon 的 PID/创建时间与前次观察相同；目标实例按工作包要求保持桌面运行，不关闭用户可能恢复的应用。

## 样本与算法差异

- 真实现场：Android 桌面，IPC/Encode 各 5 预热+50 正式，再加重建后 2 帧，共 112 张；逐张 hash、尺寸、通道、采集时刻递增核对通过。两路相同静态图标 ROI 的 BGR 字节差为 0，代表图经 Codex 检查方向正确。动态广告/时钟并非严格相同画面，不给严格加速倍数。
- 旧诊断目录限定检索得到一张可用的地下城探索负例；标签来源为 **Codex 视觉审阅，不是人类金标准**。NEXT 和 Pause 均 NoHit、设备输入为零。NEXT 与旧 `_check` 同阈值对照；Pause 只对照旧布局子函数，没有声称完整旧 OCR/Overlay 链对照。
- 没找到本轮可用的真实 NEXT 正例/多尺度/遮挡/边缘集合，亦缺真实 Pause 正例及角色/技能面板负例。不得用合成模板贴图或系统截图核销这些缺口。
- 无效/越界 ROI、缺图、坏图和资源错误统一 Error，不沿用旧代码“吞错返回零分/回全屏”的行为。这是本包三态及边界保护，非静默改阈值。
- 多 ROI 由首个主区域及其余排除区域表达；低于阈值仍保留最佳候选证据，不填可执行中心。boolean-only 不虚构置信度或目标中心。
- `reached`/`through_stair` 用 Hit 表达“已到达/已通过”，并在证据中保留依据；旧函数的 None/position 返回值由未来 M4 调用者显式承接，当前未切换调用者。
- 旧 fastForward 返回模板左上角，新的 Observation 保留真实匹配框，并额外保存 `legacy_position`；不得在 M4 接线时忽略这个坐标语义差异。
- 未把整个 Factory/Farm、StateCombat 或钓鱼流程放进 Custom。静态盘点中的任务和配置权威不变。

## 耗时与完整性成本

单位 ms，分位数使用线性插值，正式尝试全部保留。初始化单列，不与各阶段中位数相加推导总耗时。

| 路径 | 负载 | 预热 / 正式 | 成功 / 失败 | 中位数 | P95 |
| --- | --- | --- | --- | ---: | ---: |
| MuMu Extras | 1600×900 Android 桌面 | 5 / 50 | 50 / 0 | 207.51 | 221.38 |
| ADB Encode | 同一桌面，动态元素随时间变化 | 5 / 50 | 50 / 0 | 797.56 | 876.03 |
| 小包离线识别 | 4 文件 / 23,926 字节，同一模板 ROI | 5 / 30 | 30 / 0 | 32.75 | 40.32 |
| 全包离线识别 | 441 文件 / 11,939,799 字节，同一模板 ROI | 5 / 30 | 30 / 0 | 532.93 | 647.71 |

作者原资源为 437 PNG / 11,915,873 字节；成本夹具额外含 4 个有来源的测试文件，不伪称全部是正式资源。
作者 revision：`35e2151250ad2994e43bdfea2d069dae6482623efa523039484a9d6f4ba75a2a`。

| 阶段中位数 | 小包 | 全包 |
| --- | ---: | ---: |
| 初始化（单次） | 33.74 | 569.51 |
| 离线采集桥接、解码/归一化/再编码 | 82.26 | 74.61 |
| 识别前帧解码/校验 | 23.43 | 20.79 |
| 识别前完整 bundle 校验 | 3.15 | 252.96 |
| 参数预检 | 0.011 | 0.011 |
| 原生识别，含 Custom 内再次完整性校验 | 5.26 | 252.99 |
| 详情转换 | 0.10 | 0.10 |

**明显成本在全包校验，不是模板匹配本身已被证明需要半秒。** 直接识别与 Pipeline 都要保住完整性，现实现的直接路径在外层及 Custom 内各核验一次；这解释了主要开销，但不据此删除校验。
后续需要单独确定不可变资源快照的完整性策略、同次调用复用边界及失效测试，再优化。当前没有调整 frame age、阈值、完整性要求或生产预算。
实机“取帧耗时”含内层截图、PNG 路径和前台查询、外层桥接；未额外拆出 DLL 裸捕获/每一次 PNG 编解码，不能声称各内部子阶段已经独立计时。
输入/后置点击等待在本轮实机中 **未执行**，不填虚构的耗时零值。有限测试没有提供长期内存归因，RESOURCE_UNRESOLVED 不关闭。

## 验收、哈希与审核包

构建入口 `next/tools/build.py --m3` 已完整成功，收尾仅用同一 CMake preset 增量构建；每次均确认构建退出后才运行 EXE。
Gate B 前执行全套离线回归，前台查询修正后再执行最终 `next/tools/validate.py --m3`，全部通过。
最终原生 EXE SHA256：

```text
test_runtime.exe      de9ddc5efa74daab2cbe982e0d4016156f229f57c4b3598ef3cadfd2cf852181
test_recognition.exe  f2d95bdb5670a910077322f8b3cf8a0a13f1ca982965aff7d8b6d08faaff9121
test_m3.exe           de54f0830bbe19aa0ab5f4fc99baf803b83c651e4563df0fab8c61ab750c305b
wvd_m3_check.exe      c84d5c970cbb4fc8f5930e85e65e9525a01707a7fcb467cbbfad477a07c31495
```

最终测试原始目录均位于 `next/.local/`，私有索引：M2 `runtime-9hfe3jfv` / `recognition-b6zmgawe`，
M3 `m3-vision-k68m1qgx`，实机 `m3-device-a4b34a86e5a247878dda9dd21669d5e5`；前两次设备阻断/失败也保留。
汇总索引在 `m2fix-m3-1ce5ae7f95994ff19cf4c78042336e2c`；原图、配置、地址、SDK 和模型不进公开包。
早期 Gate A 的 tracked diff 不含当时新建未跟踪文件，不能单凭该 diff 重建所有中间源码；最终审核包包含完整新版源码、逐文件 hash 和最终 EXE 对应结果，补齐最终产物追溯，不伪称中间快照完整。

脱敏审核包：`next/.local/review-m2-m3-20260913/WVD_M2_M3_Review_20260913.zip`。
内含 `source/next`、固定旧算法对照、改动 diff、最终数值和样本 hash 索引；`review-files.json`
逐项记录封存字节 SHA256。`evidence/summary.json` 保存完整原生源码/EXE/hash 与保护明细。
无 SDK、EXE、OCR 模型、用户配置或真实截图；脱敏后的日志是审核副本，不取代本机原始证据。

## 继续条件

本轮停在 M4 前。下一阶段须明确处理系统导航场景适配、真实 NEXT/Pause 样本、全包校验成本，以及真实阻塞/内部重连边界。
不得据此开启完整挂机、自动游戏/VPN恢复、Spark/真机或生产替换，也不得把资源事项改为 PASS。
