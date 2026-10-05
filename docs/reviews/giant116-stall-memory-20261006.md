# candidate116循环异常与内存汇报

## 当前结论

- 用户要求收集问题、commit/push并禁止继续改动；已停止后续代码修改、构建、测试、部署及实机输入，不恢复循环。
- 正式部署仍是candidate116，17654，PID48136；程序并未闪退。请求`f2e9d5a7-376b-40c9-814b-03141aa2646d`的新100轮完成5轮，第6轮停滞。排查为保留现场调用停止，当前UserStopped/quiescent、repeat inactive，未完成轮不计成功，剩余95轮。
- 当前源码包含最后一项**未编译、未执行验证、未部署**的静止识别一致性补丁及定向检查入口。它在用户禁止继续修改前已经写入，提交用于保全，不代表修复验收。之前候选的验证记录不能证明这个补丁通过。

## 实机证据

- 命名空间`6F50586C-82C6-4975-A12D-B131B1BCA676`，run2–6的正式result.json全部Completed；run7停止前API为Running，`Task_FirstDungeon_Route0_Moving`，连续异常53602ms/60000ms，pending_inputs为空，task_step=0、悬赏阶段3。
- 停止后新截图为第十区迷宫，小地图及快捷键仍在；继续导航箭头是白色，不是黑色。不能套用“黑色按钮即到达”的结论，也不能把静止本身当已击杀目标。
- run7末段没有ADB/实例断线事件，查到的lifecycle确认仅在启动阶段。此次不能归因模拟器闪退、VPN未开或网络掉线。
- 真实现场图见[evidence/giant116-navigation-stall-20261006.png](evidence/giant116-navigation-stall-20261006.png)。原日志及动作前后PNG保留本机`next/.local/c11-flow-product/data/runs/<命名空间>/7`和同data目录的`recent-frames`，本次没有清理或覆盖。

## 已确认的链条问题

### 目标战再起后丢失返程承接

- seq629–631进入TargetCombat0/FightTarget0/Battle_Entry，确属目标战，而不是普通路上遇怪。
- seq2080确认party_death_cleared；seq2092–2094为Battle_Revive→ReviveExit→FightTarget0返回；seq2095–2098转Dispatch→Resurrect。之后seq2163再次进入普通Battle_Entry。
- 后续没有AfterTargetBattle0记录。普通战斗结束后进入宝箱、恢复，再次SelectPoint/CallRoute0，task_step仍为0。
- `tasks/dungeon_route.cpp`中FightTarget的revive/chest/blocked出口回Dispatch，目标战恢复后没有保留独立目标战身份与成功延迟/返程出口。这条承接缺口已有实际执行链证据；尚未修改或验证解决方案。不能为了绕过停滞直接把所有遭遇算作目标已完成。

### 同帧静止判断被前一分支消费

- seq3878标记点击回执确认，seq3885进入Stopped→StoppedExit后父级再次Dispatch；seq3910第二次标记点击确认，seq3923继续导航点击确认，seq3929进入Moving，此后直至停止没有新输入。
- `navigation/auto_route.cpp`的Arrived和Stopped都嵌入movement_stopped；all组合即使其它条件NoHit，也会执行静止子条件。
- 原`vision/native_recognizers.cpp`每次3秒静止采样后立即改写MovementSample.at；另一个复合请求同帧再进入时变成sample_interval。服务外层按完整请求缓存，不能保证不同复合条件共享这个时序结果。
- 代码审查确定存在这种同帧不一致路径；尚未通过故障帧跨复合回放证明它是本次停滞的全部原因，不能将源码判断写成已验收修复。
- 禁止继续改动前已添加MovementSample的帧身份/结果保存，以及`test_native_author --movement-frame <真实PNG>`入口；仅保留同帧结果，不取消3秒采样或把旧帧等待变成停止。**尚未构建、运行或部署。**

## 内存与耗时

数字直接来自正式diagnostics.jsonl及memory-lifecycle.json，MiB为字节/1048576；峰值为日志已采样最大值，不冒充整个运行的绝对峰值。

| run | 终态 | 会话wall_ns合计秒 | 已采样最大私有MiB | worker_definition_released私有MiB | 释放后句柄 |
| --- | --- | ---: | ---: | ---: | ---: |
| 2 | Completed | 379.55 | 706.4 | 74.0 | 366 |
| 3 | Completed | 383.23 | 735.9 | 75.7 | 368 |
| 4 | Completed | 359.02 | 736.8 | 78.3 | 368 |
| 5 | Completed | 345.96 | 781.8 | 75.1 | 368 |
| 6 | Completed | 372.29 | 793.2 | 80.8 | 366 |
| 7 | UserStopped | 766.82 | 809.9 | 82.6 | 365 |

- 前5轮平均会话执行368.01秒，约6分8秒；此计时来自会话performance，不包含会话外所有启动/准备/轮间等待。timing_errors均0。
- run7收尾的session_owners_alive为805.6MiB，session_owners_released为274.0MiB；worker_definition_released为82.6MiB。大部分高峰随会话及执行定义释放，但释放后仍有小幅净增长，不能宣称无泄漏。
- 最后runtime_sample系统提交约88.4%，说明本次并非仅在此前93%以上极端压力才出现停滞。
- OpenCV正式probe的dungFlag全屏900×1600、67×61模板估算单次工作区约169.8MiB，最高并行估算约249.4MiB。这指出大搜索区域/并行识别会形成高峰，但只是工作区估算，不是分配栈证据，不足以认定809.9MiB全部来自OpenCV或该模板。
- 内存来源仍为未归因：需要分别核对匹配工作区、OCR懒加载、执行定义和会话持有对象的创建/释放边界。此次仅收集已有证据，没有新增长测、扩大日志或改内存策略。

## 留存与后续边界

- run7事件4312行、动作耗时2229行、诊断35行、最近截图保存297次；相应dropped/failed均0。result.json为停止终态，不能以运行时文件临时显示0字节判断日志丢失。
- 源码检查与真实静止图probe已经执行；该probe确认auto_route_moving=Hit、dungFlag=Hit(0.96196)、resume_unavailable=NoHit(白色占比0.83883)。单次movement_stopped=NoHit/first_sample只能说明第一帧采样，不能当作跨帧检查通过。
- 本次源码提交还保全此前candidate71–116累积的工作台/战斗/悬赏/恢复/素材和对应报告；正式profile、运行数据、.local候选及.vscode不纳入。最新未验证补丁与运行中的candidate116必须区分。
- 后续只有用户重新要求后才继续修复或部署；100轮没有完成，不自动恢复95轮，不声称内存稳定性或全部异常已闭环。
