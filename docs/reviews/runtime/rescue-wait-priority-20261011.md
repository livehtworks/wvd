# 救人进度、等待节点与入口优先

## 修复范围

- `party_death_observed`撤销旧准备选择，不再重载策略。成功救人保留同一遭遇的已消费动作、剩余条目和当前策略组；真正确认再起仍走`resurrected`重载。旧Python在普通救人时也曾ReloadStrategy；本轮按用户明确要求纠正，不继承该缺陷。
- 巨人计划的目标遭遇不再携带硬编码3000ms。`after-target-battle`公共步骤默认直接继续；使用现有流程编辑器的wait节点设置等待位置和duration_ms，可放在任意可编辑步骤的连线之间。保存的公共定义是权威，不另建等待执行器。
- 已归属且已结算的目标，推进任务点只核同一unit/point与phase=3的账目。不用加载过渡页再次证明已经完成的战斗；下一输入仍有独立当前帧场景/目标/未决输入门禁。
- 迷宫入口优先当前迷宫场景和计划导航；SelectPoint先确认当前导航/地图场景，再选任务点。导航子图当前目标Ready优先于通用遭遇分派，但已记录无路、无目标和旧返程提示仍保持原终止语义。
- 打开恢复面板不再固定等2000ms，也不接受未变化的迷宫页作为打开成功。观察真实面板或遭遇；漏点只在同一角色、当前迷宫和原输入保护成立时重试。未发送恢复不消费尝试，不伪称HP/MP已恢复。

## 原始现场

原实例0364D50B-F3AB-4790-92B3-76ED27162C39、请求giant-100-shared-target-20261011-eda98023979441e996642952a3d61fdc：

- run7–9进本后约6–7秒才提交标记导航；不是固定6秒sleep，而是多层入口/通用分派的累计耗时。
- run8宝箱结束事件1418，恢复入口1425/1429，1429又退回Entry，1440交出遭遇，1448进入第二场战斗。期间约13秒没有恢复输入。精确失配原帧已从旧240帧环中淘汰，不能用日志倒推某个像素/ROI必然错误，亦不能把后来的截图冒充该帧。
- run9已记录目标结束后，AfterTargetBattle0耗时3.70秒；AfterTargetResult0又耗时8.95秒、425次actual_matches/20次ocr_calls，随后进入第二场战斗。已完成目标的重复页面确认与硬编码等待已撤出。
- run10出现party_death_observed，源码在该事件重载整套策略。此处是明确状态错误，与再起不同。

原run1–9各份保存Completed回执已按SHA索引，共9成功；修复暂停的run10不计成功。证据与暂停状态在`next/.local/rl-261011/before.json`，原run7–10及最近帧只读复制保留。总目标仍100，后续仅续91；不计更早22轮。

## 验证

- 最终`next/.local/rl8-contracts-261011/receipt.json`：app/recognition/runtime共49入口complete=true，输入edbf0cd9f1b14b9f147bfe580e2c6228c190b9be88828a03b6035e32aec7c04c。新增Application实际compile_task_graph/保存库snapshot_closure组合验证，不只给工厂注入完整作者库。
- 真实状态所有者测试：六名角色先用完一次性技能，普通救人后保持剩余动作；再起后才恢复起始动作，旧准备选择不能复用。
- 实际作者公共流程/编译器/执行器测试：默认无等待、明确插入250ms等待、未知页不能凭页面单独确认未归属目标、已结算目标不等待另一个页面。
- 原恢复工厂/执行器：第一次角色点击不生效，原角色受保护重试后面板出现并完成一次恢复尝试；未发送、未知送达和疗效未验证边界保留。
- 正式profile只读副本的四种默认跳轮覆盖组合通过，图3032节点。日志`next/.local/rl-final-giant-261011.log`；不是四轮游戏。
- 正式21份保存公共定义的只读副本接入八种完整受控场景，均通过、pending=0；八份轨迹在`next/.local/rl-linked-proof-261011`，对应打包输入f8d209...。它们属于应用依赖清单修正前的工厂组合证据，不冒充最终应用准备或实机通过。
- 新增长链验证触发两处编译期栈溢出：CompiledWorkflow::validate节点visit、compile_native_program作用域visit。CDB栈在rl-stack/rl-stack-linear2日志；四处按节点递归遍历改为迭代，2000节点真实编译/校验通过。原4608节点和8层子调用合同保留，没有扩大线程栈。
- rl1中止、rl2等待夹具layout未补齐、rl4长链暴露第二处递归；rl3/rl5属于后续应用修正前通过。rl6新增应用夹具误把API投影当存储文档；rl7实际默认全自动配置进一步发现ActorChanged无人引用节点。改用ProfileStore正式读取契约，并仅在非空技能catalog下生成这个消费技能路径专用节点；空技能/显式Auto能力保持。最终rl8重跑全部49入口，不合并较早部分结果冒充通过。

## 部署与实机边界

第一次候选f8d209...构建/部署、公共定义读回通过，但真实Application准备请求giant-remaining-rescue-latency-20261011-26b9992cf0c24d3c80586f982f11a1a1报FLOW_REFERENCE_MISSING:after-target-battle，0尝试/0输入/0成功。已保留失败请求和failed-preparation.json；原因是native_public_steps快照清单漏接，现补入同一清单，没有运行时fallback。该空闲候选已关闭。

最终edbf0cd9f1b14b9f147bfe580e2c6228c190b9be88828a03b6035e32aec7c04c已由原build.py/package_functional.py及manage_service Validate/Deploy完成。EXEfbd857d06a2206ddfdf0efefc57f76fe03a603295db393ecc0f8b10b6062692c与构建回执匹配，服务9AE5E79F-B0D6-4B40-98CA-E4848C3C4EA1/PID56248/start_filetime134361861844404263。原端口/data-root及profile保持，既有保存流程未覆写；新公共步骤revision0358ebb01a82aa27468e0f3d911b5c77bf261f11d6ced4a454f0df96f7b1f31a已读回，默认无wait。

续跑giant-remaining-rescue-latency-20261011-6ef43c732e214760b80a17444c06a141/repeat_count91已实际Running/run1，公会→跳轮→标记导航→巨人战斗有新帧与真实输入。记录rl-261011/retry-intent.json、retry-submission.json、resumed.json；原失败准备请求不覆盖、成功0不计，累计9+当前completed_cycles。VPN CONNECTED，小时监控指针已切到当前批次。

自然run1的入口事件522→标记Choose542约3.26秒，随后发生真实导航输入（543/545）并确认（549）；提交耗时约3.5秒。原6–7秒已减少，但不是即时级别，不能宣称所有捕获/门禁开销均已优化。

run1目标战斗返回2333→目标结算2336→后续公共步骤2339→任务点确认2346，后续公共步骤至任务点确认约0.02秒；没有固定3秒或额外AfterTargetResult等待。打开恢复角色2360距离目标战斗返回约3.21秒；本次没有宝箱，不能冒充原宝箱后13秒的同条件对照。返哈肯导航2439/2444已提交，约13秒后2468出现路上遭遇；2476/2841是同一次额外战斗的进入与返回，不是两次额外战斗。此遭遇不是恢复入口原地等待期间触发。

run1已保存Completed/result_saved=true/quiescent=true/storage_error空，真实提交/住宿结束；当前run2已Running并推进标记导航。累计10/100=原9+本续跑1，进度与结果SHA索引在rl-261011/live-progress.json及launch-ledger，不把失败准备或修复暂停计成功。

已有新候选一轮完成、入本导航及无宝箱战后恢复自然样本，尚无旧宝箱后13秒的同条件对照，也未故意制造死亡。成功救人延续策略的状态所有者回归通过，不冒充自然救人成功或长期稳定性证据。内存仍RESOURCE_UNRESOLVED，不新增WPR/ETL/堆采集，不自动commit/push。
