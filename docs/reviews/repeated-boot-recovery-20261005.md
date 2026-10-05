# 第29轮恢复后停止

## 现场与进度

- candidate114批次`B1FADA4C-750C-4F99-AE82-1CAC08F2FDE3`前28轮均Completed；第29轮于北京时间约20:19落盘Interrupted，`EVENT_REPLAN_CROSSES_ACTIVE_HANDLER`，361.852秒。此前26轮加本批28轮为54/100，剩余46轮，失败轮不计成功。
- 第29轮最后已恢复到爱丽丝施放友方技能的战斗画面，工具quiescent、循环failed。游戏/模拟器仍响应，停止原因不是当前进程退出。
- 证据保留在`next/.local/c11-flow-product/data/runs/B1FADA4C-750C-4F99-AE82-1CAC08F2FDE3/29/`；`diagnostics/1.png`为再次重启前标题页，`diagnostics/2.png`为最终停止前战斗页。旧终态不修改。

## 调用链

- seq730截图ADB返回closed；seq732绑定实例需重新启动。约42.5秒恢复实例/游戏后进入根声明的ReconnectBoot处理器，原普通菜单重新分类。
- ReconnectBoot的Title点击已送达，但页面仍为标题。该动作没有声明新帧补点，约60秒未进展触发再次重启（seq1605）；不把已送达等同于游戏已响应。
- 第二次恢复7.01秒成功后再次进入同一根恢复处理器，旧Title仍有未确认回执。新的Boot最终恢复战斗，处理Pause的输入在seq2724已确认。
- 新处理器返回，尝试按根声明的replan回Entry；`resume_event`在检查旧Title真实后置之前，看到旧Boot事件帧就以跨活动处理器停止，seq2735–2739。恢复已完成却不能交回主业务，是本次直接停止原因。

## 修复范围

- Title添加3秒间隔的原页新帧重试，沿用同一个结果期限；标题页仍在且许可有效时才补点，页面变化后停止补点。
- 只允许同一owner、同一事件ID/处理器/根重规划目标、明确on_device_restart的恢复链合并退出。每个旧pending仍须新帧命中原后置、身份与输入代次有效；未知送达须底层清理确认。其它活动处理器仍不允许跨越。
- 已用生产执行器验证4个定向场景：同链已确认结果可回根且不重放输入；旧结果仍不命中时继续等待并保留pending；未知送达清理失败保持停止；其它活动处理器仍保留跨栈拦截。原受保护输入、异常重启与启动等待回归通过。测试只隔离设备/识图，未制造实机断线；新增检查分支初次编译缺局部断言/驱动函数，补齐后通过。

## 部署与续跑

- candidate115部署17654/PID21880，服务实例`8E475AA0-C907-4B46-8020-AB6AD4987CBE`，保留原data和profile。旧后台通过管理器结束，未关闭游戏或模拟器。
- 请求`2dc0b6e1-c6c6-42e1-b807-ed89171abcc6`续跑46次，命名空间`B05ED249-CFED-493C-B05A-7965E978E9E8`。run1已Running并接回当前战斗；新完整轮尚未完成，旧54轮保留。
- 证据：`next/.local/repeated115-restart-check.log`、`repeated115-critical-check.log`、`repeated115-exception-check.log`、`repeated115-boot-check.log`、`repeated115-deploy.log`，部署前新鲜截图`repeated115-before.png`。未强制重启制造相同故障，同链二次重启的自然实机复现仍待观察。

## 内存证据

- 本批第1轮worker_joined约68.5MiB，第28轮94.5MiB；最后八轮在94.5–108.5MiB之间回落，句柄363–365。无法仅凭净增长归因具体分配。
- 第29轮失败前约821MiB，session释放后304.9MiB，线程join后123.7MiB，整批配置释放后120.4MiB/312句柄。没有821MiB一直不释放的证据。
- 失败时系统提交约91.5%，进程榜可见MuMu约3.08GiB、ChatGPT约2.03GiB、WVD约0.80GiB；部分进程不可读，不能用可读榜单解释全部系统提交。系统压力与最初ADB中断相关性保留，未证明OOM或OpenCV泄漏。
