# 后台生命周期整改：交付与部署

## 启停交付结论（13:30）

本报告记录启停整改交付时的现场；后续游戏循环状态以`docs/project-status.md`为准。用户13:41另行要求新开独立100轮，已通过正式入口启动，不续接旧71轮。

2026-10-01用户恢复执行后，本轮后台启停与部署整改已完成，最终候选为candidate53。工作台入口为 http://127.0.0.1:17654/，界面可直接“退出工具”；打包目录同时提供启动和退出批处理。后续替换版本由管理入口自行关闭核实归属的旧后台，不再要求用户寻找后台PID。

循环没有恢复：暂停前累计29/100，剩余71轮；当前后台Idle、busy=false、quiescent=true，设备未连接。本轮未启动游戏任务，未关闭游戏、模拟器或VPN。后台生命周期证据不计为游戏任务成功。

## 实现与职责

1. `native/app/service_instance.hpp`在Application构造前取得数据目录OS独占锁，持有至任务、设备与应用析构结束。同一数据目录不允许两个有效后台同时加载、写入。
2. GET `/api/v1/service`返回实例ID、PID、进程创建时间、EXE、端口和数据目录。POST `/api/v1/service/shutdown`核对实例ID，关闭新任务准入并发出取消；HTTP回执写入后才关闭监听，随后沿既有路径回收任务、设备、工作线程。旧页面的实例ID不能退出新后台。
3. HTTP退出使用控制请求队列及after_send回调，不被识别工作队列阻塞；收到回执与进程真正退出是两种状态，不以HTTP断线冒充退出完成。
4. Vue“退出工具”在配置写入期间禁用；有未保存更改时使用页面内dialog。取消保留草稿，确认放弃不会保存草稿。收到退出回执后停止轮询并隐藏游戏操作入口，显示“退出请求已接收”。
5. `tools/manage_service.ps1`是启动、退出与部署管理入口。先取得管理锁，再核对实时实例ID、PID、EXE、创建时间和数据目录。重复Start复用同候选；不同候选Start或Deploy等待已确认旧进程退出后启动新版。监听先关闭时等待旧数据锁释放。未知端口归属不按同名全局杀进程。
6. `native/platform/windows/process.cpp`复用现有Windows进程封装实现同一EXE的`--launch`模式，仅继承NUL输入与两条日志句柄；启动器返回PID即退出，后台独立运行，不挂在临时Shell的输入输出管道上。
7. `tools/package_functional.py`打包管理脚本并生成启动/退出批处理。成功启动绑定实际数据目录和端口，双击入口不会误开默认目录中的另一份配置。本轮不改变游戏业务和技术底座，不添加第二运行所有者。

### 运行文件

- `service.lock`：OS进程所有权，退出自动释放，不承载用户配置。
- `service.json`：发现用实例元数据，不是业务权威；残留记录不可单独授权终止进程。
- `service-manager.lock`：序列化管理操作，OS自动释放锁。
- 候选目录`service-launch.json`：启动参数绑定，不修改profile。
- `service-logs/`：每次启动独立stdout/stderr与启动器错误记录，既有日志不清理。

## 实际验证

只执行对应本轮改动的限定检查，没有跑旧任务矩阵、资源长测或游戏循环。

- Windows PowerShell 5.1真实原生进程检查通过：重复Start同实例；旧ID退出409；另一端口复用数据目录被OS锁拒绝；Deploy替换并等待旧进程退出；profile revision保持；退出清理期间立即Start收敛后启动；默认绑定Stop结束对应后台。
- 最终检查使用candidate53和专属临时空数据目录，未接设备。日志：`next/.local/logs/service-lifecycle-check-native-launch-ps51.log`；证据根：`%TEMP%/wvd-service-lifecycle-e62ecb02e43248d8bf5ee0b49d5303d6`。检查进程38412、33372、2960均退出。
- 一个真实Vue/原生服务用例通过，没有Mock：改旅店间隔后取消退出保留草稿，再确认退出不保存草稿，退出提示出现，游戏入口移除，API停止可达。用例在专属隔离目录执行；后台PID24808已退出。日志：`next/.local/logs/service-lifecycle-ui-final.log`，截图：`next/.local/service-lifecycle-ui-20261001/exit-confirmed.png`。此检查发生于最终原生启动器调整前，前端与退出处理代码之后没有变化。
- 正式设备仅做连接与只读取帧：candidate50/PID14868连接MuMu后，版本替换结束旧进程和取图宿主PID428；candidate51重新连接取得设备锁，随后真实退出批处理结束PID10648及取图宿主PID9588。没有发送游戏输入。
- 实际批处理发现Windows PowerShell 5.1额外继承管道句柄，stdout/stderr重定向及单独NUL stdin不足以修复；旧调用只有在后台退出后才结束。因此增加原生限定句柄启动，而不是仅把READY当启动成功。
- 最终candidate53替换candidate52/PID38160后，旧挂起调用已正常返回。候选53真实退出批处理结束PID34492，再由其真实启动批处理启动PID8916；启动调用在5.45秒内返回0，后台随后仍运行。
- 最终正式界面已刷新，退出按钮可用；现场仅一个automationd，没有取图宿主。截图：`next/.local/service-lifecycle-deployment-20261001/workbench-ready.png`。

## 最终部署与数据核对

- 候选：`next/.local/c11-flow-product/candidate53`。
- PID8916；实例`756BC2CC-4284-4791-876E-0E112769F5E6`；端口17654。
- EXE SHA256：`7570c12229c6067e6506a8e2a6d8f864bb5ee204195d62e572408247bdb21cfd`。
- 正式数据目录仍为`next/.local/c11-flow-product/data`。
- profile revision仍为`f31710ea57ed823580030af7389d4f9af2c8ab158188225166df7ea051cbddb4`。
- profile文件SHA256部署前后一致：`93e9ec76b6e6e709ca9ab221a92ea23ea1b41cbcc8183bc81010d43b290c2f38`。AUTO_START_CLASH、繁中、法术+地裂与指定角色7级不变。
- 暂停快照：`next/.local/service-lifecycle-pause-20261001/snapshot.json`。历史run4的result.json、action-timing.jsonl仍在原实例`AD8B2855-D8C9-461E-A84F-63E644DFF893`的正式runs目录，没有改写成完成。
- 暂停时交接说明归档至`next/.local/service-lifecycle-deployment-20261001/paused-handoff.md`；当前文档不继续沿用未部署状态。
- 没有commit/push；未撤销或误纳入用户既有修改。

## 日常使用与边界

打开工作台后可点“退出工具”。重新打开或退出可双击candidate53下的“启动WVD原生版.bat”“退出WVD原生版.bat”。替换版本使用管理入口，例如：

```powershell
& next/tools/manage_service.ps1 -Action Deploy -CandidateRoot <new-candidate> -DataRoot <existing-data-root> -Port 17654
```

退出等待上限默认120秒；底层回收超时会保存明确诊断且不并行启动第二后台，不宣称任意SDK阻塞都能立即取消。无需用户日常寻找PID或关模拟器来释放工具。原游戏长期稳定性及未解决加载期限问题不由本轮启停检查代替验收；剩余71轮仅在后续明确要求后恢复。
