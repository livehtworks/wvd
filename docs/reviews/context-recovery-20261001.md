# 游戏前台与退出恢复整改

## 范围与原因

- 用户授权完善游戏/模拟器退出恢复，沿用正式入口、绑定实例2、当前VPN及配置；本轮没有commit/push要求，不恢复历史100轮。
- 原55/100会话的第56轮在进入荒屋后取得桌面横屏帧。游戏ROI先报错，外层又在生命周期观察前因未确认输入退出。缺Android退出栈，不能据此断言OOM或具体崩溃原因。
- 修复复用唯一所有者链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor → DeviceSession。没有第二恢复线程、全局杀ADB/模拟器、降低识图阈值或裁剪越界ROI。

## 生产代码

1. `runtime/native_flow_ports.cpp`：游戏会话在识图前核对截图的前台包；非游戏帧产生`GAME_NOT_FOREGROUND`读取故障，不送进游戏ROI。只读设备预览仍允许真实横屏桌面。非游戏像素单独保存，不能作为输入授权或最后有效游戏帧。
2. `contracts/observation_fault.hpp`：连接代次交接与应用恢复分开。`ObservationReconnect`只证明真实连接变更；`ObservationRecovery`另外记录应用重启/前台恢复，不伪造代次。
3. `devices/device_session.cpp`：核对同一绑定实例及创建标识；退出则拉起该实例，离线则重连。随后按真实pid区分存活游戏切回与退出游戏拉起；按配置确保安卓VPN，丢弃旧桌面元数据缓存，并确认游戏已在前台。不会强停仍存活的游戏。
4. `runtime/flow_executor.cpp`：切回前台不推进业务、不清pending、不重发输入。游戏重启后，仅明确可重试、无副作用绑定且送达已知的菜单回执可标记`interrupted_by_application_restart`，交回已有业务阶段重新选路，绝不标记成功。付款、跳轮、报告等保护输入继续保留，由启动处理器及原后置证据核对；无法确认则停止，不盲目重放。
5. `runtime/native_run_coordinator.cpp`：即使输入未确认也可只读记录生命周期现场，观察异常单独记日志，不覆盖原错误。整段恢复的副作用保护仍保留。非游戏截图按诊断像素保存，避免横屏身份被错误当成游戏视口。
6. 冷启动实测发现Clash10秒内部期限在tun已经建立时仍误报`VPN_PERMISSION_OR_PROFILE_REQUIRED`。改为沿用120秒生命周期步骤窗口，外层停止/连续读故障期限继续有效，结束前复核VPN实际状态。
7. 自然启动公告没有原处理分支，曾在公告页耗尽Boot窗口。补充公告标题＋底部关闭双证据；只裁标题、不识别日期或正文，复用已采集的繁中关闭文字。作者语义源、原生冻结目录、启动/阻塞页配方及生成资源包同步，不无条件点击任意关闭按钮。
8. `tools/package_functional.py`：同步作者JSON时，同时复制语义目录引用的扩展素材并更新manifest哈希。只处理`next/resources/images`中实际存在的源素材，校验路径、排除符号链接，不扫描用户日志或mod。candidate56因新增公告图未进入包而被准备阶段拒绝；不能把只更新JSON或HTTP接受请求当成正式任务已启动。

## 验证与证据

- 有限生产执行器检查：读取恢复/耗尽/停止、前台切回、菜单重启重选路、送达未知及副作用绑定保留，共7场景。每场景只提交原输入一次；证据`next/.local/logs/context-recovery-flow-20261001.log`。
- 生产NativeFlowPorts检查：1600×900桌面在ROI前拦截、恢复900×1600游戏帧、只读桌面预览；证据`context-recovery-capture-20261001.log`。替身只隔离设备，不冒充实机验证。
- candidate54从关闭的实例2经正式入口拉起；VPN10秒旧期限导致启动失败，原结果保留在`data/runs/325602F9-BDAA-441B-A617-882DAEB1CFDA/1`。
- candidate55按正式入口重新打开Clash，确认tun0 UP且游戏进程/前台存在；自然公告未处理导致Boot超时，原结果保留在`data/runs/A69C88F1-69A3-4831-B600-D96EC40B2DB6/1`，不能称整轮成功。
- 公告真实截图：`next/.local/logs/context-recovery-game-now-20261001.png`；标题页反例：`context-recovery-game-start-20261001.png`；素材`next/resources/images/boot_announcement_header.png`截自前者`[400,135,100,55]`，没有生成或改写文字。
- candidate56准备阶段报`NATIVE_IMAGE_MISSING:image/boot_announcement_header.png`，未开始实机任务；修复资源同步后发布candidate57。公告生产识别器的真实正例、标题页反例及关闭按钮共3项通过，证据`context-recovery-announcement-recognition-complete-20261001.log`。
- 最终部署candidate57：17654，PID21848，服务实例`6595B5F0-8C67-4DFD-9539-DF8A27BECA8F`，EXE SHA256为`5ECE1A6D2899F59933E00EF81017E8E6BB260683B72B492BB976EDF430AAC69F`。部署脚本自行退出旧后台并接管同一data目录，没有并行生产服务。
- 实机请求`context-recovery-20261001-device`，运行目录`next/.local/c11-flow-product/data/runs/9F2486FA-2564-45F0-B22F-9D94133FC85D/1`：Completed、3/3业务段，426.2157793秒，战斗1次、悬赏报告1份、200G住宿1次；61次输入/0拒绝。报告及住宿pending均false，静止且落盘，循环未开启。最终截图`context-recovery-final-city-20261001.png`确认回王城。
- 退出恢复证据：启动阶段探针发送Android HOME后，Android事件记录主游戏pid3798在19:22:51.841死亡，pid7695在19:22:53.660启动；不能断言死亡原因是OOM或与HOME无关。读取恢复7.297秒，明确记录`application_restarted=true`、`input_replayed=false`、连接代次未变；原子会话以`device.application_exited`保留NotCompleted，由既有协调器重新进入启动及原业务阶段，最终整轮完成。此时业务输入尚未提交，不冒充实机未确认菜单中断测试。
- 存活切回证据：导航/返程阶段打开已有Clash前台，游戏pid7695保持不变，正式恢复记录`application_restarted=false`、`foreground_restored=true`、`reconnected=false`，随后原任务继续完成。证据`context-recovery-device-proof-20261001.json`、`context-recovery-android-events-20261001.log`及`context-recovery-final-run-20261001.json`。

## 未覆盖边界

- 本轮验证了实例2关闭时的正式冷启动；未在运行中主动关闭模拟器，故未证明“模拟器退出且存在未确认业务输入”的所有实机情况。
- 未确认菜单、送达未知和副作用绑定的保留/重新选路契约通过有限生产执行器检查；没有声称这些故障都已自然实机出现。
- 单轮通过不证明所有随机事件或长期资源稳定性。历史100轮仍停止在55/100，不把本次独立验收轮加入旧计数；本轮未commit/push。

## 数据边界

- 正式配置revision保持`f31710ea57ed823580030af7389d4f9af2c8ab158188225166df7ea051cbddb4`，文件SHA256保持`93E9EC76B6E6E709CA9AB221A92EA23EA1B41CBCC8183BC81010D43B290C2F38`。
- 旧55轮完成记录和失败第56轮不改写，不与本轮验收混计。旧日志、Python入口、mod和dist不清理。
- 本轮控制验证只触及绑定实例/Clash/游戏生命周期；禁止宝石购买、抽卡、出售。没有关闭另一Android12实例0。

## 回滚

任务停止且静止后，使用`next/tools/manage_service.ps1 -Action Deploy`选择candidate53，保持同一正式data及17654；旧候选、原配置和历史记录均保留。不得同时运行两个正式后台，也不通过自动回退混用新旧实现。
