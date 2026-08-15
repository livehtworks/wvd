# 本地稳定性与性能优化记录

本文档记录当前 fork 中围绕稳定性、截图性能、重启恢复和打包瘦身的本地改动与后续计划。

## 当前本地改动

当前分支相对上游 `arnold2957/wvd@master` 包含若干功能提交和说明文档提交。它不是一个只包含 `NEXT` 改动的最小 Pull Request 分支。

涉及文件：

- `.gitignore`
- `requirements-build.txt`
- `用于本地测试的打包脚本.bat`
- `src/script.py`
- `src/utils.py`
- `src/gui.py`
- `docs/local-stability-and-performance-notes.md`

### 打包与本地配置保护

- 新增 `requirements-build.txt`，本地打包使用独立 `.venv-build`。
- 打包脚本在构建前备份 `dist/wvd/config.json`，构建后恢复，避免覆盖用户本地配置。
- `.gitignore` 增加 `.venv-build/`。
- 打包脚本继续排除若干大型无关模块，并保留本地化文件编译流程。

这部分对应提交：

- `11ac2b7 Make local packaging reproducible`
- `cca3e00 Preserve local config during packaging`

### 战斗目标与 `NEXT` 相关

- 调整战斗中选择敌方目标的恢复逻辑。
- 当单体技能选择 `NEXT` 目标失败时，增加更保守的恢复路径，降低敌人在右侧边缘、只露出部分身体时点不到导致卡住的概率。
- 删除本地临时扩大 range 的思路后，改为围绕现有 `next` 模板识别和点击逻辑做更稳的兜底。
- `NEXT` 识别改为亮色字形遮罩匹配，减少 `next.png` 暗底背景对识别结果的影响。
- 当 `NEXT` 不可用时，新增敌人头顶白色倒三角目标标记 `combatTarget.png` 作为兜底锚点。
- 选中目标时不再只点单个位置，而是点击标记下方横向矩形区域，提高右侧边缘敌人、大体型敌人和前后排高度差场景的命中率。

这部分主要对应提交：

- `e26b54c Improve combat target recovery`

注意：当前分支还没有实现“scrcpy 高速截图后端”。Clash 自动恢复已作为可选项加入，默认关闭。

### 角色头像识别修复

- 修复 `CheckRolePortraitMatch` 中翻译函数 `_` 与局部变量占位符冲突导致的 `UnboundLocalError`。
- 该问题曾在实机运行时触发：

```text
UnboundLocalError: cannot access local variable '_' where it is not associated with a value
```

这部分对应提交：

- `ff37ee5 Fix role portrait matcher translation scope`

### Clash 与恢复链路

- 模拟器设置区新增“模拟器重连/重启后自动启动Clash并恢复VPN”复选框，默认关闭。
- 设备连接成功后，如果该选项开启，会检测模拟器内 Clash 包并通过外部控制 action 启动 VPN。
- VPN 恢复后如检测到启动前游戏在前台，会切回游戏。
- 修正强制重启模拟器后没有写回新 ADB device 的问题。
- 修正按 PID 关闭 MuMu 进程时使用了错误 `taskkill` 参数的问题。

### 诊断与误判降低

- 新增未知界面诊断：当 `IdentifyState()` 多轮无法识别状态，或 `state:None` 即将因为假死/宝箱超时/战斗超时重启前，会记录一组关键模板的最高匹配结果。
- 未知界面诊断会按原因限频保存截图，避免 `logs` 因每秒截图快速膨胀。
- Pause 暂停层识别改为更保守：仅在游戏重启后的短窗口，或状态连续无法识别后检查；同时用角色界面、恢复按钮、技能详情、底部 `Close` 作为反证，反证命中时不会点击恢复。
- Pause 候选被反证排除、Pause 确认命中时都会限频保存现场截图，方便后续用真实样本继续调阈值。
- 修正点击“停止”时刚好打断战斗等待目标导致的异常：`FindCoordsOrElseExecuteFallbackAndWait()` 可因停止事件返回 `None`，`StateCombat()` 现在会识别该情况并正常退出。
- 任务线程增加异常兜底：即使 Farm 线程未捕获异常退出，也会通知界面执行收尾，避免按钮长期停留在“停止”状态。
- 地图目标选中后，`AutoMove.png` 对应的是浮动说明提示，不是可点击按钮本体。若点击地图 Auto-Move 控制后仍停留在地图，且该提示仍存在，说明 UI 层仍能响应但地下城移动/物理逻辑没有接管，当前会记录截图并按“地下城物理逻辑冻结”仅重启游戏。
- 自动战斗兜底前会先关闭技能弹窗。当前补充覆盖技能确认弹窗：若检测到底部 `OK`，会点击对应的 `Cancel` 位置取消技能，避免角色未匹配到策略时反复点击 Auto 固定坐标并卡在技能界面。
- 宝箱拆陷阱坐标 `[515,934]` 靠近战斗技能区，不能在状态切换后继续连点。当前拆陷阱连点前会确认仍处于宝箱上下文；若已进入战斗、地下城移动、复活或 ambush，会立即停止 disarm 点击并把状态交回地下城状态机，避免宝箱转战斗时点开下个角色不该使用的技能。
- Pause 恢复逻辑不能递归调用 `IdentifyState()`。现场曾出现真实 Pause 持续数小时，工具每轮递归重新进状态识别并堆积日志/截图，最终 `wvd.exe` 被 Windows 标记为无响应。当前改为同一轮循环内恢复 Pause；连续 6 次仍恢复失败时，按 AutoMove 同类问题处理为“战斗/地下城物理逻辑冻结”，记录截图并仅重启游戏。
- GUI 日志框不能从工作线程直接操作 Tk 控件，也不能无限保留日志文本。当前日志处理器改为 Tk 主线程批量刷新，并限制主日志显示行数；摘要日志使用同一套线程安全刷新逻辑。
- FFXI 挖矿补给回城链路中，曾出现 `City_RoyalCityLuknalia` 高分命中并点击后仍停留世界地图，随后通用等待 `Inn/openworldmap/dungFlag` 50 次失败并重启游戏，重启后又落到标题页循环。当前将世界地图目标点击抽成专用确认逻辑：点击目标周边多个安全点；若仍检测到 `worldmapflag`，先重新定位目标并重试，连续多次仍失败才保存 `worldmap_target_enter_failed` 现场图并重启。
- FFXI 挖矿补给回城链路中，若世界地图点击已经进入王城，但 `Inn` 等目标状态尚未稳定识别，原确认逻辑会把 `City_RoyalCityLuknalia + [500,1100]` 作为通用 fallback，导致在王城内继续执行世界地图点击。当前改为：进函数先检查城市锚点；点开世界地图后等待 `worldmapflag`；点击世界地图目标后若已离开世界地图，只等待 `Inn/openworldmap/dungFlag` 稳定出现，不再执行世界地图目标 fallback。
- FFXI 挖矿补给不是靠重新选择 `FFXIStone` 队伍完成，必须进入旅店住宿才会补充镐子。当前在补镐子链路完成回王城与重新集结队伍后，会立即执行 `StateInn()`，不受普通休息间隔影响。

### 截图与模板匹配性能

- `LoadTemplateImage()` 增加内存缓存，避免同一模板反复从磁盘读取和解码。
- `_check()` 在常见路径减少整图 copy，只在必要时 copy ROI 或匹配结果。
- 减少部分状态判断中的重复截图。
- 高频模板补充默认 ROI，减少整屏匹配范围。
- 截图前台检查节流，避免每次截图都调用 `dumpsys window | grep mCurrentFocus`。
- `CheckIf()` / `CheckHow()` 增加同一帧内匹配结果缓存，同一张截图里重复检查同一模板和 ROI 时不再重复执行 `matchTemplate`。
- 亮色遮罩匹配缓存模板 mask，避免 `NEXT` 和目标倒三角识别时反复灰度化、阈值化和膨胀模板。
- 战斗目标兜底连点改为批量 ADB shell，减少多次 `input tap` 的往返成本。

这部分对应提交：

- `9805121 Avoid full screenshot copies in matcher`
- `7954333 Cache template images in memory`
- `c19075a Reduce redundant state screenshots`
- `16f540b Add default ROIs for combat templates`
- `d2d97d2 Throttle screenshot focus checks`
- 待提交：非 ROI 匹配缓存与批量点击优化

### 说明文档

- 新增本文档，用来记录本地稳定性、性能、截图后端和上游贡献拆分建议。

这部分对应提交：

- `64ec6e9 Document local stability and performance notes`

## 当前观察到的问题

- ADB `exec-out screencap` 截图链路仍是主要瓶颈，本地实测约 350ms 级别。
- `ScreenShot()` 仍通过每次启动 adb 子进程获取整帧画面，无法达到流式截图速度。
- 游戏重启链路已有“先重启游戏，再恢复 ADB，再重启模拟器”的雏形，但部分异常可能过早升级为模拟器重启。
- 已修正 `KillEmulator()` 中按 PID 关闭模拟器的 Windows 命令，避免已知 PID 路径实际没有正确关闭目标进程。
- 强制重启模拟器后改为通过 `ResetDevice()` 写回新的 ADB device，避免后续继续使用旧连接对象。
- `restartGame()` 的崩溃计数可能长期累积，成功重启游戏后是否应重置仍需确认。
- `logcat -d | grep ...` 可能把诊断命令自身异常误判为游戏崩溃线索。
- Clash Meta 在模拟器重启后可能没有自动恢复 VPN，需要独立恢复与验证链路。
- `dist/wvd/logs` 可能快速膨胀，当前本地曾出现约 120MB 日志和截图。
- Pause 误判需要继续用真实截图样本校准；当前先通过反证模板和触发时机降低误点概率，暂不引入 OCR 强依赖。

## 截图方案评估

### ADB 截图

当前主链路使用：

```text
adb -s <serial> exec-out screencap
```

优点是兼容性高、实现简单、无需额外组件。缺点是每次截图都要启动命令、通过 ADB 搬运整帧原始数据，速度上限较低。

### scrcpy

本地验证官方 `scrcpy v4.1` 可连接 MuMu：

```text
scrcpy -s <ADB地址> --no-window --no-audio --record <file> --time-limit=5 --max-fps=30
```

验证结果：

- MuMu Android 15 可连接。
- `--no-audio` 必须启用，否则默认 opus audio encoder 可能失败。
- 可录出 900x1600 视频，约 23fps。
- 前几帧可能是黑帧，后端需要等待非黑帧。

兼容性判断：

- 只要目标电脑能通过 ADB 连接模拟器，scrcpy 方案理论上可用。
- 需要随程序分发或让用户配置 scrcpy 可执行文件。
- 不同模拟器、Android 版本、显卡驱动和编码器可能影响稳定性。
- 需要保留 ADB 截图兜底。

`py-scrcpy-client 0.4.1` 不建议直接作为正式依赖。它内置 `scrcpy-server-v1.24.jar`，依赖旧版 `adbutils` 和 `av`，在当前 MuMu Android 15 环境中可以握手，但无法收到视频帧。

### MuMu 增强截图

本机 MuMu 安装目录存在：

```text
<MuMu安装目录>/nx_main/sdk/external_renderer_ipc.dll
<MuMu安装目录>/nx_device/<Android版本>/shell/sdk/external_renderer_ipc.dll
```

MAAFramework 通过 `nemu_connect` 和 `nemu_capture_display` 调用该 DLL，直接从模拟器渲染器获取 RGBA 图像缓冲区。该方案理论上更快且无损，但依赖 MuMu 版本和闭源 DLL，适合作为实验后端，不适合直接替代默认截图。

当前 fork 已完成独立 PoC，并已接入 `ScreenShot()` 主链作为优先截图后端：

- PoC 脚本：`tools/mumu_ipc_poc.py`
- MuMu 根目录：从配置的 `EMU_PATH` 自动反推，不应写死为某台电脑的安装路径。
- DLL：优先从当前 MuMu 实例目录查找 `shell/sdk/external_renderer_ipc.dll`，再尝试 MuMu 通用 SDK 目录。
- 实例号：读取配置中的 `EMU_INDEX`
- `nemu_connect` 成功，handle 为 `1`
- `nemu_get_display_id` 返回 `0`
- `nemu_capture_display` 返回 `900x1600`
- IPC 原始图为 RGBA，转换为 BGR 后需要垂直翻转；与 ADB 截图对比平均像素差约 `4.4-4.7`
- 连续三轮 100 次截图均 `100/100` 成功
- 三轮平均耗时约 `12-15ms`，对照 ADB 30 次平均约 `352ms`
- 退出时 `nemu_disconnect` 已正常执行

接入方式：

- `ScreenShot()` 通过 `ScreenshotBackendManager` 优先使用 `MumuIpcScreenshotBackend`。
- MuMu IPC 初始化、截图或句柄失效时，自动断开 IPC 并临时回退 `AdbScreenshotBackend`。
- ADB 截图仍保留原有解析、警告处理和异常恢复逻辑。
- `ResetDevice()` 会主动让截图后端失效，避免 ADB/模拟器重连后继续使用旧 DLL handle。
- 当前实机 smoke：第一次截图约 `89ms`（包含连接），后续约 `11-15ms`，返回 `(1600, 900, 3)`。

注意：MuMu DLL 当前会向 stderr 输出 `connect not same day`，但返回值、分辨率、图像内容和连续截图均正常。当前仅作为诊断信息观察，不作为失败条件。

### 恢复链实机验证

当前分支在本机配置的 MuMu 实例和 ADB 地址上验证：

- 普通 ADB 连接后，Clash/VPN 检测为已连接。
- 仅关闭游戏后，旧 ADB device 曾变为 `offline`；随后按现有恢复链执行 `ResetDevice(force_restart_adb=True)`，能重新拿到设备、确认 VPN、启动游戏并恢复 IPC 截图。
- 强制重启模拟器后，能重新连接配置中的 ADB 地址、确认 VPN、启动游戏，并重新建立 MuMu IPC 截图 handle。
- 重启模拟器后的 5 次截图均返回 `(1600, 900, 3)`，耗时约 `10-27ms`。

结论：高速截图后端没有破坏现有“游戏重启 -> ADB 恢复 -> 模拟器重启”的稳定性链路；后续若遇到 IPC 失败，应优先按日志确认是否已触发 ADB fallback。

## 后续建议

### 稳定性

- 修正 `KillEmulator()` 的 PID 关闭命令。
- 将游戏重启、ADB 恢复、模拟器重启拆成明确的恢复阶段。
- 每个阶段保存清晰日志：触发原因、执行命令、验证结果、升级原因。
- 游戏重启成功后考虑重置或衰减崩溃计数。

### Clash

- 已增加可选项：模拟器重连、ADB 恢复或模拟器重启后启动 Clash 并恢复 VPN。
- 使用 Clash Meta 外部控制 intent：

```text
am start -n com.github.metacubex.clash.meta/com.github.kr328.clash.ExternalControlActivity -a com.github.metacubex.clash.meta.action.START_CLASH
```

- 启动后优先用 `ip addr show tun0` 验证 VPN 接口，再用 `dumpsys connectivity` 检查 `Transports: VPN`。
- 如果启动前游戏在前台，恢复 Clash/VPN 后会尝试切回游戏。
- 首次授权时会尝试识别 Android VPN 系统确认弹窗并点击确认；如果仍未连接，会保留日志并打开 Clash 供人工检查。

### 识别速度

- 当前 MuMu IPC 已成为优先截图后端，ADB 保留为兜底。
- 截图耗时降低后，主要瓶颈转移到全屏 `matchTemplate`、固定等待和 ADB shell 往返。
- ROI 优化需要知道图标大致位置，后续应结合具体事件逐个补，不适合一次性硬塞全局 ROI。
- 不依赖坐标的优化优先级更高：同帧匹配缓存、亮色遮罩缓存、批量点击、固定等待改短轮询。
- 固定等待改短轮询需要结合动画和网络状态实测；未验证前不建议大面积缩短 `Sleep()`。

### 打包瘦身

- 已移除未使用的 `scipy.optimize.curve_fit`、`scipy.signal.find_peaks` 和 `win10toast.ToastNotifier`，避免 PyInstaller 因空 import 拉入 SciPy 与通知库。
- `pyinstaller` 与 `babel` 仅用于本地构建，已从运行依赖移动到 `requirements-build.txt`。
- 打包产物中 `cv2`、`numpy.libs` 仍是主要体积来源；后续如果继续瘦身，应优先评估截图/识图链路是否能替换或精简 OpenCV。
- 发布包不应携带运行期 `logs`。
- 根目录 `巫术.png` 和 `resources/images/press!!!.png` 是较大的已跟踪图片，但删除前需要确认是否用于 README、发布页或人工调试。

## 上游贡献建议

建议拆成多个小 Pull Request：

- 打包脚本保留 `config.json`。
- 修复模拟器 PID 关闭命令。
- Clash 自动启动与 VPN 验证，默认关闭。
- 模板缓存和 ROI 优化。
- 移除未使用依赖并验证打包。
- `scrcpy` 高速截图后端作为实验功能单独讨论。

这些改动由 AI 辅助整理，并经过本地人工验证。后续提交上游时应在 Pull Request 描述中明确测试方式和兼容性边界。
