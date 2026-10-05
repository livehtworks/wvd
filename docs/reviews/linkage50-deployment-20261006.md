# 目标战交接修复部署与50轮采集

## 提交与部署

- 用户在限定工作包交付后明确追加commit/push、部署及50轮实跑授权，本次按新授权执行。
- 源码提交`96024f58202ca1fcc228e9d7a1a5e297db0b173a`已推送个人远端`fork`的`agent/local-stability-notes`。用户原有`.vscode/`未纳入、未修改。
- 从干净源码身份冻结build-input，实际构建`next/build/Release/automationd.exe`；沿原package_functional.stage生成独立candidate117并通过原管理器完整文件预检。前端源码未变，保留对应已构建产物。没有用旧EXE冒充新构建。
- 正式管理器Deploy正常退出candidate116/PID48136，再以相同17654端口和原data目录启动candidate117/PID28944；新服务实例`CFA621DC-1D1B-47F2-8BDE-ACA642692B4D`。没有强杀无关进程、开旁路服务或重启模拟器。

## 保存流程与配置

部署前备份原profile及wheel-open。通过既有revision CAS只修改wheel-open的Download后置：从包含下载按钮本身的any条件，同步为作者源“下载按钮消失”。修复旧条件可能把同一下载页当作动作结果已确认的问题；没有替换整份流程或重排节点。

前revision为`4e23c829ec7487fbd20424a8a50cea1fa8825ecef6faa8085d7a383832534d36`，后revision为`b8b646a9a46cb5d1b665053033daafda197dadc5254a58f1a1af3ea25ba03dc9`。结构化对比确认除revision与该后置外全部字段相同。用户profile原文SHA256保持`3e873d08fb9135f558c3e3348b3bfffff3b73555c63a9176cc75cff274ccb87c`，战斗方案未修改。

没有人为制造下载/断线；旧下载差异已消除，但自然下载分支仍需后续日志确认，不能据CAS写入宣称实机分支通过。

## 新50轮

- 任务为当前正式`GiantBounty / [悬赏]巨人`，不是蝎女；繁中、沿用原策略/住宿配置，按原Application循环所有者运行。
- 新请求`3916784c-63ac-44c9-b285-c86bcb9fc029`，`repeat=true / repeat_count=50`。旧100轮的5次完成不计入新批次。
- 已确认run`57479A08-CE93-4390-A8F2-66EE8BB5AB65/1`实际Running、非quiescent、repeat active/目标50，初始完成0轮。请求accepted/preparing不是此处运行证据；后续正式Running快照已独立保存。
- 部署前新鲜PNG确认第三章要塞城市；正式EnsureVpn观察记录`connected=true / instance_running=true / application_running=true / application_foreground=true / vpn_ready=true`。没有因为PC代理存在就假定安卓VPN已打开。
- 启动后已执行荒屋/诅咒之轮到凯旋跳轮，存在实际input.result和关键PNG；尚未宣布首轮或50轮完成。

## 采集与证据

原正式logging已为debug，memory/performance/recognition均开启，保持1000ms内存细采样间隔；没有额外更改配置。动作耗时、执行审计、异常与关键帧由原RunStore保存；滚动图上限仍240张/128MiB，不增加外部循环或持续额外取帧线程。

新run已落盘对象计数和OCR owner初始化配对：owner1初始化前私有内存244826112字节，初始化后392577024字节，句柄962→995。这是同进程操作范围内的差，不是独占分配栈；释放边界和剩余占用等待正常本轮终态及后续自然轮次对齐，不宣称内存稳定或805.6MiB全部已归因。

本机完整证据：

- `next/.local/deploy-linkage50-20261006/`：native-build/package/deploy日志、原profile与wheel-open备份、CAS请求、部署前PNG、50轮请求/接受/Running快照。
- `next/.local/c11-flow-product/candidate117/DELIVERY_STATUS.json`：源码提交、构建输入、完整资源/EXE/前端哈希；service-launch固定原data及17654。
- `next/.local/c11-flow-product/data/runs/57479A08-CE93-4390-A8F2-66EE8BB5AB65/`：该批原执行/耗时/内存日志和终态，逐轮独立目录。
- `next/.local/c11-flow-product/data/recent-frames/`：原滚动关键帧及历史图；窗口内Get-Item.Length可能暂时报告0，须用实际读取及落盘错误计数复核，不能据0元数据判断空日志。

后续异常按原恢复策略处理；遇无法恢复、受保护输入不明或多人死亡选择会保留诊断并停止，不把失败算作完成，也不改50轮次数掩盖中止。
