# 再起前死亡过渡页导致循环中止

## 实际故障

candidate117的50轮巨人批次完成16轮，第17轮于146.797秒落盘Failed/quiescent，原因`party.death_interrupted`；repeat inactive。不是客户端/游戏/模拟器闪退，也不是已经点击再起后失败。run为`57479A08-CE93-4390-A8F2-66EE8BB5AB65/17`，停止后前台仍是游戏。

最后调用栈在目标战Actor内触发wvd-party-death，处理器SelectedHandler4连续点击救人中心；最后一次输入epoch27已确认结果、无送达未知，随后经过OtherBlocking→BlockedExit终止。原目标上下文仍active、task_step=0，没有把失败计为成功。

## 原帧复核

通过正式识别Service的只读probe重放故障PNG与停止后的新鲜PNG，未向游戏输入：

| 帧 | 再起识别 | Pause | input_clear | 说明 |
| --- | --- | --- | --- | --- |
| 原失败帧 | NoHit，OCR实际文本thevil死亡，0.997421 | Hit，8个文字连通块、暗色比例0.916 | NoHit/local_overlay | 正常死亡提示过渡，不是再起选项已经出现 |
| 当前页 | Hit，文本再起，0.999548；中心[450,755] | NoHit | Hit | 两个选项为再起、接受死亡；第二项未点击 |

旧Pause算法只凭中心暗色/亮字及连通块布局，把动态姓名死亡提示误判为Pause。救人处理器看到dead消失且input_clear失败就直接OtherBlocking→recovery failure；它没有等待自然出现的再起菜单，因此没有进入原战斗的revive出口。

## 修复

只修改既有dismiss_party_death的OtherBlocking：改为250ms只读Poll，在原90秒处理器期限内重新分派Dismiss或Cleared。仅真实救人页的Dismiss仍有输入资格；普通过渡、再起选项尚未出现的黑白动画、未知覆盖层都不盲点，不把等待本身写为救人/再起成功。既有网络检查、取消、原期限及BlockedExit超时出口保持有效。

Cleared只有新鲜原就绪场景、dead消失且input_clear才结清death_prompt，再交回原战斗Actor→Encounter的Revive出口；不在救人处理器内复制复活/目标战图，也不跨原输入回执重放技能。全局Pause匹配算法未在本轮改动，避免未经真实Pause反例核对扩展修改范围。

## 验证与边界

独立Windows构建通过。新增窄入口`test_native_author --death-transition`使用生产工厂、业务确认和FlowExecutor，仅隔离页面叶子与设备送达，三个场景通过：死亡提示→再起；死亡提示→恢复战斗；过渡等待中用户取消。三个场景过渡期均只有原救人输入1次，不提前清死亡账目或写resurrected，不增加Auto/付款/接受死亡输入。

真实两帧识别与执行器过渡检查是不同证据，不冒充修复后的完整实机再起成功。当前游戏保持再起页，原批次16/50、剩余34；此次调查没有点击接受死亡、重启游戏或恢复循环。

candidate118已通过完整候选预检，并由正式管理器在同一17654端口/同一data目录部署，旧PID28944正常退出，新PID31728、实例52B5D599-214B-47A9-9C38-38753B458ECF。后台Idle，原16/50失败快照独立保留；没有恢复循环或操作游戏。源码修复未在本次调查中新增commit/push，候选DELIVERY_STATUS保留真实HEAD及未提交补丁哈希，不冒充已提交构建。

本机证据在`next/.local/revival117-20261006/`：failure.png、current.png、两个probe JSON、build.log、death-transition.log、三个evidence JSON，以及package/deploy日志和部署前16/50状态快照；candidate118的DELIVERY_STATUS固定产物身份。不覆盖原失败文件。

## 续跑入口补齐

用户随后要求继续，原入口请求34轮`ec1da7fb-f66a-4dd9-a79d-d9c640a7af68`实际进入Running，但Boot_Entry的非common就绪条件只有boot_ready，未接再起页；正常再起页面持续停在Boot_Poll。已在异常重启前停止，run`11865E47-1FC8-434A-8AB2-E3A7AA3500AD/1`为UserStopped/quiescent、完成0轮，不冲减原剩余34。现场周期帧`20261006T054858441_r1_f16.jpg`确认为再起/接受死亡，未重启游戏或模拟器。

启动检查现在把RiseAgain作为明确的可交还业务场景，继续要求无blocking_screen/无普通剧情；不会点击再起或标记复活成功。复活仍由原悬赏InspectRevive→ReviveForInspection及迷宫Resurrect承接，不创建新复活逻辑、不修改识别阈值或预算。窄入口`--boot-revival`核对繁中生产启动图只读完成、输入为零；完整实机承接以续跑日志为准。

限定检查已通过（BOOT-revival-handoff PASS），候选119已沿正式管理器部署原17654/data，旧PID31728退出，新PID39784/实例A993E8CB-8CC2-4FA7-840F-2AE106F71865。实际源码含本轮未提交补丁，不宣称commit/push。剩余34轮请求`e352205f-d7b0-4aac-93e1-fbc2f44fd62c`已接受；停止的入口运行不计轮数。构建、限定检查、打包与部署日志为本机同目录boot-revival-*，原16轮成功及第17轮失败均保留。

该请求实机run`6F203752-F7C8-4956-8020-80566D1AD79B/1`正确退出Boot并进入原Task_FirstDungeon_Resurrection：再起输入[450,755]获得confirmed，后续[450,750]已送达，但随后黑屏未确认结果；连续异常62.469秒进入原重启/重连，恢复后旧输入保护以Interrupted/revival.outcome_unconfirmed收尾，完成0轮。保留未确认，不记复活成功；这条复活跨重连保护仍是未闭合项，不因入口修复宣称整链通过。

新鲜截图`resume119-reconnected.png`确认当前为游戏启动声明页，前台游戏/设备连接均在。按本次继续授权从正式入口新启动剩余34轮，请求`28c87b89-f91e-4733-94de-5ab46b96c351`；旧输入不重放、失败批次不计成功，不改变配置或旧审计证据。

该请求已实际Running，run2/repeat active目标34、完成0。正式启动已执行声明/资源下载分支；最新周期帧`20261006T055803025_r2_f84.jpg`仍为黑屏，流程处于Boot等待/既有异常恢复，尚未证明重新进入战斗或完成新轮。保留循环运行，不将API的Running包装成正常游戏链通过。

## 续跑批次终态核查

用户随后询问是否结束：正式服务当前Completed/quiescent，repeat state=completed、active=false、completed_cycles=target_cycles=34，无终态错误。只读逐份核对同一namespace的run2–35，共34份result.json全部Completed；run2和run35本机文件落盘时间分别为2026-10-06 14:08:46、18:24:20（北京时间），文件落盘时间不是独立业务完成时钟。run35业务task_step=2、bounty_cycle.completed_cycles=1、reports_remaining=0。原16轮加续跑34轮合计50轮完成，两个中止尝试未计入；本次没有启动新循环或操作游戏。此核查证明批次完成，不证明无中途异常或内存已稳定，原未确认复活跨重连证据仍保留。
