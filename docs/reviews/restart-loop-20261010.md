# 第9轮反复重启现场排查

## 当前状态

故障批次部署为输入hash `d1b45197726826370f8ad9605ab196d389b4b9f8434124843882935717aa697a`，服务实例 `E398993B-FFA7-4516-8331-0819EDF84AF5`。批次 `fa827b34-27bc-40cd-8636-41bd65c3d6c7` 已完成8/96；run9停在住宿后的离店/启动恢复链，非第9轮完成。当前修正版部署见文末。

发现重启循环后已通过正式停止接口取消run9及循环；API为UserStopped、busy=false、quiescent=true、repeat.active=false。服务仍可操作，未关闭模拟器，未清日志/图片，未更改用户配置。以下修复已写入源码；最终候选的构建、验证和部署状态以文末收口记录为准，循环保持停止。

## 已证实的问题

run9事件文件记录6次 `diagnostic.application_restart`，seq为9454、9759、10021、10273、10528、10783，均为 `CONTINUOUS_EXCEPTION_TIMEOUT`，关联 `Task_Inn_Leave`。随后恢复记录均报告application_restarted=true。首次还经历METADATA_TIMEOUT、DEVICE_INSTANCE_RESTART_REQUIRED及设备重连，不能说始终只做了简单游戏前台切换。

第一张重启前图片 `diagnostics/1.png` 是正常“背包已補充完畢”提示，继续箭头约在(840,1136)，不是网络故障或宝石购买。`next/native/games/wvd/supply/inn.cpp` 中supply_arrow限定 `[775,940,125,120]`，下边界1060，不包含真实箭头。ContinueSupply的场景和目标均依赖此条件，因而无输入可执行，正常提示被留在未知等待中，60秒后进入游戏重启。

第2至第6张重启前图片已是要塞城市。进一步逐事件核对：seq9708–9712实际经过ReconnectBoot_Ready、ObservationPhaseEnd及Terminal；seq9713回到Task_Inn_Leave，seq9714记录EVENT_RESUME_UNCONFIRMED。生产Service在原图6上也命中AtCity和完整Boot Ready条件，blocking_screen为NoHit。因此不是缺城市素材或Boot识别没有结束。实际缺陷是FlowExecutor::resume_event先检查旧父节点的超时，再检查新画面的resume_guard，已恢复城市被旧Leave期限拒绝，导致重复恢复和重启。

FlowExecutor在连续异常期限到达后先复核当前观察，再发bound_game.restart；游戏重启后保留父业务栈，推入ReconnectBoot事件。此次父栈为Task_Rest→Task_Inn_Leave。启动处理已收敛，但恢复接续顺序拒绝了有效新画面，导致同一业务停点反复重启；不是正常循环每轮重启，也不是本轮WVD_CONDITION_DEPTH复发。

## 保留证据及后续边界

证据根：`next/.local/c11-flow-product/data/runs/E398993B-FFA7-4516-8331-0819EDF84AF5/9/`，包括result、execution-events、action-timing、diagnostics及六张重启前原图。最新正式发布程序在 `data/published/repeat-ae9c571af2f4d741a3adededc53aac360b9cc62562c64b149a7a9b29176b59e0/`。

## 修复范围

- 对照旧src/script.py的StateInn：选择房型、确认住宿，返回旅店菜单后PressReturn；没有等待背包补给信息或满血的步骤。新版确认旅店菜单后使用ADB BACK，菜单退出即结束住宿子步骤。上层有住宿回执时不重复等待城市/补给文字；未住宿路径仍保留城市判断。
- 剧情/补给信息出现才继续，不要求出现，不读取道具名称或“补充完毕”正文。继续箭头及住宿确认搜索整个下半屏；離開名称搜索菜单下部，不依赖狭窄的固定高度。保持原阈值、实际匹配中心、网络遮挡和宝石购买保护。
- Boot增加可选正常信息页处理，但不接管已知迷宫奖励页；故事仍要求AUTO控制和无选项，不把任何黄色箭头都当故事。
- 恢复接续先看当前resume_guard，再在NoHit时检查旧节点超时。未决输入仍逐项核对原后置、身份和送达状态；不清回执、不盲目重放。
- 同页返回键补试只放行KEYCODE_BACK=4，不能扩展成HOME/POWER等系统按键重试。沒有调整60秒异常窗口、重启次数、用户配置或付款安全边界。

## 限定验证

最终源码限定验证均通过，日志在next/.local/architecture-audit-20261010/：

- inn-exit-final-binary-frames.log：最终测试产物的生产Service使用原图1/6和真实冻结包，完整ContinueSupply/Boot信息页在提示图命中、在城市不命中；完整Boot Ready在城市命中。没有Error。
- inn-exit-fixed-fixture-contract.log：可选提示有/无、BACK菜单退出后没有城市帧仍完成；旧Leave过期后恢复到城市只接续一次，无重启/重放/未决输入。
- inn-exit-verified-transitions.log及隔离同名证据目录：既有住宿pending加载等待、城市不伪造付费、金币房型重办语义保持。
- inn-exit-boot-progress.log、inn-exit-boot-revival.log及对应独立证据目录：启动进展/网络补试、再起交接与未决输入保护保持；inn-exit-chest-handoff.log保留战斗/宝箱交接、目标归属和无路线停止。
- inn-exit-final-contract.log保留一次夹具失败：故事箭头ROI扩宽后与信息箭头成为同一条件，原夹具错误地将非故事场景的箭头强制设NoHit。已只修外部观察夹具，未改生产逻辑/放宽断言；最终日志为fixed-fixture-contract。

受控观察只用于复现业务分派和恢复接续，不冒充真实游戏一轮；原图复核只证明对应页面识别，不制造实机异常，不启动新轮。长期循环稳定性尚未证明。

## 部署收口

- inn-exit-product-build.log及对应.local/logs/inn-exit-*记录Web/原生构建、依赖/冻结输入和打包成功；inn-exit-service-stop.log、inn-exit-service-deploy.log记录旧自有服务退出及新版就位，没有关闭模拟器。
- 原17654/原data-root；新服务实例3891BA20-7485-49CD-B15A-60D58E90F34D、pid22400。网页HTTP200，服务API正常，当前Idle、busy=false、quiescent=true，未启动循环。
- product_inputs_sha256=f068d3cb93428fae31df90f3905afa0781a59e638396150ac658595aab3da33d，1289项；EXE=8eae4ed0b3c351f6cc4655958cec21edd4c95be5fde31bfa819661d393b2492e。source_commit仍fd6b617，包含未提交修复，不将旧提交号冒充全部本轮源码已提交。
- profile SHA256仍3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C，revision仍d26988cd440cd5f55fbf2ca34e0e4b7b1aaf54dc16d1717d154e0a065b865cab。没有改用户配置或保存流程，没有新增实机轮次、付费或内存采集。本次源码/限定验证/部署完成，实机整轮与长期稳定性未验。
