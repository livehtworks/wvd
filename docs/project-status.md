# 项目当前事实

## 最新离线收束

- `ddc3366`稳定性/UI工作包K01–K08已完成指定源码整改、隔离验收和独立候选整理，见[收束结果与证据](reviews/closure-ddc3366-result-20260927.md)。元数据恢复分类、编辑锁/任务切换、App唯一状态镜像、紧凑三分组、迁移展示退役、磁盘准入和迟到转场均有对应证据。
- 源码已提交为`055341d`并推送个人远端`fork/agent/local-stability-notes`。用户随后单独授权部署但禁止操作游戏/续轮，2026-09-28已将相同产物复制为candidate33并启动。原候选`next/.local/cl-ddc-927/candidate`及其构建身份保留，状态仍为`BUILT_NOT_GAME_ACCEPTED`；部署不等于实机验收。原资源稳定性边界继续保留。

## 当前产品与运行

- 当前操作入口为 `http://127.0.0.1:17654/`，原生候选 `next/.local/c11-flow-product/candidate33`，数据根仍为上级的`data`；部署时PID34328。运行EXE SHA256为`084cde9d2bfb1f7259900c835326d89c73f115832a7a3433885592b5fdc0214f`。启动后只读核对Idle、busy=false、quiescent=true、设备disconnected，未启动任务或恢复循环。旧Python入口、配置、mod、日志和旧dist未改动。
- 基线 `fcaecdad8beb068d20a3be17526fd25bc8861068`；本次源码提交包含观察恢复、循环次数、动作计时及转场统一修复，并附审核报告。用户已明确要求commit/push，目标仅个人远端`fork`的`agent/local-stability-notes`；用户无关`.vscode/`不纳入。提交不代表部署或恢复循环。
- candidate31正式工作台实跑蝎女完整一轮 Completed，266.519746秒，55次输入/0拒绝，三段业务完成，悬赏余量0，标准房200G提交1次，已回王城。1/1轮自动停止、结果落盘与静止均确认；不能用它替代candidate32完整一轮的验证。
- 100轮的已完成基数为旧实例`94500C1E-8BA1-4CA6-B5ED-B3DBBCEDBEA0`的run2–5，共4轮；原请求`1917dae0-c10f-4147-a227-7d915c83c430`。后续续跑请求及部署过程见本轮报告。停止按钮取消当前轮及后续轮，服务重启不自动恢复会话。
- 用户随后明确授权强制关闭旧工具并部署。candidate31的run6正常停止、静止及落盘，付款/报告pending均false；设备断开后只强制结束旧工具PID36348，启动candidate32 PID39856，未关闭游戏、模拟器或VPN。
- 历史candidate32实例`04BAA4AD-78C5-41B4-AF62-C4976BCE56B9`，请求`260941e6-6eef-4ddc-bf0d-3179f6244bd6`：run2–39完成38轮，run40在20:31:27因`FLOW_INVOCATION_TIMEOUT:Task_TimeLeap_Entry`失败。加旧版4轮共42/100，剩余58轮，中止尝试不计入。此次部署前确认busy=false、quiescent=true、result_saved=true、repeat.active=false，先释放工具设备连接后仅结束旧工具PID39856。游戏当前画面未获取，不沿用旧王城截图作为当前事实。
- 沿用繁中、实例2、法术+地裂、指定角色7级、每轮200G住宿；当前另用VPN保持不动，自动Clash关闭。普通资源按任务使用，禁止点击关联绿色或紫色宝石的“購買”；不扩展到任意购买、抽卡或卖装备。

## 当前架构与本轮改动

- 唯一产品链：C++20 Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor；设备为MuMu IPC取图、ADB只读元数据与生命周期、scrcpy输入。Vue工作台使用同源API。Maa已归档，不参与当前构建。
- Application是循环唯一所有者。`repeat:true`且省略`repeat_count`为一直循环，提供正整数为指定次数；每轮仅在完整业务、落盘和静止确认后计数。
- 只读瞬态故障在原Session调用栈内退避重读，保留pending、原提交时间与业务状态。连续故障窗口60秒，剩余时间传至读取，取消沿原门禁传递；NoHit、资源错误和输入送达未知不混作读取故障。
- 重连核对绑定实例、创建标识和端口，跨代次凭单独交接证据核对原结果，不改旧回执before。实例明确退出才启动并沿既有启动流程恢复；正常NoHit/网络慢不重启模拟器。
- 入本按已确认后继页面收敛，每个fallback前复查目标，同页菜单走新帧重试。44条入口静态盘点见本轮报告，不表示实跑44个任务。
- 住宿记录实际提交与成功回执，有限补试不清账；送达未知只核对结果。宝石购买配方已注册并用于住宿确认反证，不是每帧全图扫所有异常。
- 失败诊断分别保存最后有效帧与元数据失败原始像素，后者不可授权输入；命令、期限、耗时及恢复过程写入现有日志。最近帧240张/128MiB/15秒限频，异常PNG独立保留。
- 公共流程、战斗方案、作者配置仍由原生链承接；本轮未更换技术栈或增加外部循环守护器。
- candidate32动作耗时明细已在真实运行中启用：新实例run2的`action-timing.jsonl`已出现输入accepted、input.result confirmed及节点耗时记录；不是仅构建通过。64MiB/轮上限和写入异常单独报告。详见[计时口径](reviews/action-timing-20260927.md)。

## 证据与剩余边界

- 本轮报告：[观察恢复修复](reviews/observation-recovery-20260927.md)。分层契约：[架构](../next/docs/architecture.md)。操作注意项：[执行注意项](execution-notes.md)。
- 读取暂错、ADB断线、实例真实退出、送达未知和付款补试本轮未自然出现，记NOT_OBSERVED；不制造故障、不跑旧矩阵或资源长测。源码接线不等于异常已实机通过。
- 单轮通过不证明长期资源稳定、其它任务或所有随机事件通过。既有OpenCV资源未归因边界仍保留；100轮按实际日志更新，不能预先写成功。
- 郊外启动旧问题：Boot未接受该页，任务级InspectOutside返城得不到执行机会；原现场靠手动回城收场。本轮源码已加入郊外/哈肯启动场景，尚未部署或实机验证，不能把此前手动收场写成自动恢复成功。
- run40同类转场问题已统一修复：跳轮提交后只等返回，不回选章；补齐大地图剧情、小地图迟到哈肯、自动战斗、住宿恢复及公会返回出口。见[统一排查报告](reviews/transition-convergence-20260927.md)。相关修复现已随candidate33部署，限定时序检查通过，尚未实机验证。
- 用户最新要求只commit/push和部署，不操作游戏、不恢复循环：42/100不变，剩余58轮不启动。仅替换工具服务，未取真实截图、未关闭或重启游戏/模拟器/VPN。
- 早期候选和旧能力证据见[本轮前事实快照](archive/project-status-before-observation-20260927.md)及其关联报告；不得把旧截图、旧EXE或旧轮次当成本轮结果。
