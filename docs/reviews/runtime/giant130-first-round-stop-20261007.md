# 巨人30轮首轮停止核查

## 当前结论

candidate130服务仍为PID43136、实例31B4D601-16B9-43D5-9196-E74A932843FB，未闪退。请求d599b18c-7a3d-4448-82a7-f95678588fe6在首轮515.349秒后Failed/combat.unowned_skill_detail，完成0/30，repeat inactive、quiescent=true、details_complete=true、secondary_errors为空。此次只读核查，未操作游戏、重启或恢复循环，未改产品源码。

## 错误链

正式证据目录：next/.local/c11-flow-product/data/runs/1ABAA4DD-74A9-4C18-A262-A3B0F72497F7/1。

1. execution-events的seq3466至3488：Skill13确认，记录成功并结束该行动子流程，frame197。随后重新进入战斗行动Entry。
2. seq3505/3507：进入UnexpectedPopup、UnownedDetail。turn.cpp中popup由详情/确认/关闭任一命中组成；UnexpectedPopup只检查battle与popup，后继直接recovery(combat.unowned_skill_detail)。
3. seq3508至3523：实际发现网络弹窗，NetworkOverlay_RetryZhHant成功点击重试，随后等待清除。真实frame210是“發生錯誤，請確認網路環境後再試一次。重試”，不是技能详情。
4. seq3643至3647：NetworkOverlay_Cleared、NetworkBudgetEnd、Terminal后回到UnownedDetail，没有回战斗入口重新分类，继续原失败节点。最终diagnostics/3.png是战斗画面、右下“連接中”，无技能详情弹窗。
5. seq3652确认应用仍运行且在前台、设备连接且实例未退出；最终按combat.unowned_skill_detail停止，而不是内存保护或设备闪退终态。

## 识别复核

读取真实recent-frames/20261007T115345938_r1_f210.png及当前pack素材，使用与Service模板路径一致的BGR TM_CCOEFF_NORMED、下半屏ROI[0,600,900,1000]复核：

- combat_skill_confirm_zh_hant最高0.899671，命中网络“重试”按钮区域，超过配置0.82。
- combat_skill_detail_zh_hant最高0.403986，未达到0.82。
- close最高0.539775。

这是离线同图/同方法复核值，不冒充原运行逐模板打分日志。它明确复现了确认模板对网络按钮的误匹配；错误分类又被任一按钮即可成立的popup规则放行。不是单纯ROI过窄，也不适合只抬阈值掩盖。

## 修复范围

- 技能详情需场景组合证据，不能只凭共用按钮外观判定；网络弹窗必须与技能弹窗区分。
- 异常处理结束后，重新观察并验证原错误条件；已消失则回到合法战斗观察入口，不能执行悬挂的无条件失败节点。保持不乱确认未知技能、不切Auto的边界。
- 后续针对本次网络原帧、消失后的连接中帧及已有真技能图核对，并覆盖真实执行器“异常处理成功后错误条件消失”的恢复链，不用长循环替代这个确定性缺陷的验收。

## 内存与其它异常

本轮更早曾CONTINUOUS_EXCEPTION_TIMEOUT、ADB_TRANSPORT_FAILED，设备观察为instance_exited；随后RestartInstance、EnsureVpn和StartApplication均确认，恢复后进入战斗。这是已恢复的前置异常，与最后停止分开记录，不能声称全程无重启。

本次收尾记录齐全：worker_definition_released约110.80MiB，worker_joined100.74MiB，heap_resources_optimized36.54MiB，batch_payloads_released36.88MiB；5堆统计complete=true、实际allocated17490679字节。停止清理与新增逐堆日志已落盘，但仅一个失败轮，不能判断长期增长解决。动作耗时、事件日志均complete且dropped/failed为0；异常图保存3张，近期截图saved182、dropped/failed为0。
