# 宝箱到恢复入口延迟修复

## 已查明

原部署17A75D99/2的事件3643到3679：奖励继续输入到打开角色面板约25.1秒。中间AfterReward、Done、Dispatch、Resume、Heal Entry、Requested、Begin、Open0重复取新帧及分类；相关节点约21.8秒、22次OCR调用、10次取图。模板匹配约1.8秒，识别其他处理约16.5秒。这段没有固定30秒等待。OCR调用包含缓存，不能将22次说成22次推理，也不能把recognition_other全部算作模型时间。

## 修复

- revival_prompt已有战斗排除后，以原迷宫、角色、恢复页面锚点排除正常页，保留单人/多人死亡保护；未知页仍执行原OCR。运行配方与编译资源闭包同步，不改变ROI/阈值，不混扫英文。
- 恢复Requested观察合入Begin确认：healing_required业务准入、当前页面证据、请求确认及原子账目仍由生产链承接。仅减少重复观察，不给恢复、战斗或整批新增低容错限制。
- 没有增加关闭救人、取消救人、Auto或盲点输入；原网络、Pause、救人、多人死亡人工停止和Unknown未决输入保护保留。

## 验证

- app/recognition/runtime定向合同46个入口通过，独立data-root；回执`next/.local/chest-heal-latency-20261011/contracts-accepted/receipt.json`，product_inputs_sha256为26a3f50f12599f4741ee7ec2a4cc2516110b32e665898f3df6881fc5170f1d2b。
- 真实模板构成的受控900x1600正常页：三种正常锚点的再起OCR调用均0，未知页仍1；这是生产Service执行，不是实机截图/耗时。指标在contracts-accepted/supply-plan/metrics.json。
- 原始迷宫/角色面板两帧经真实Service各自命中且互相排除：healing-real-frames-accepted.log。原帧SHA分别d9605e20830c51cfba76d03ed94411a895c0ca6b33b8804cfb8858c88dd48ad2、c23323c59b6c7ba4e7913b403bef97fd73f6bcabfaaca75318aac4687dfb604c。
- 救人、技能详情两张历史原帧经真实Service/门禁/执行器组合通过：recorded-rescue-accepted/results.json。只替换设备传输；救人首个输入仍450,800，不宣称实际复活成功或整场救人通过。
- 早期失败完整保留：独立夹具漏登记繁中模型、MSVC字符串/JSON比较、旧图片manifest与额外模型的成员不一致及一次错误的CLI pack路径。均未删除/降低断言或生产资源校验以求通过。

## 部署与循环

原服务在用户暂停后正常退出，没有关闭模拟器/游戏。正式build.py及冻结打包成功，新部署DC9A8D75-7AD8-4CE4-A3AE-B2B87CE6A854/PID56924，17654，EXE SHA273df0882979982118d9547e0c26a72fbe84cc876e05c70b26170d6acdf7901b，资源revision19750612e30ebb27e08675801066cc7299012faaab060185efa45981c908ffdf。

新100轮请求giant-100-heal-fix-20261011-0700c9f9f1944e478320b65f7dbdf0d0，经Application正式入口submitted/run1/Running；原批次成功2轮不计入新目标。profile和保存流程未改，小时巡检已接续新批次。不将Running或离线通过当作100轮/长期稳定验收。

## 实机恢复记录

新部署run1在返哈肯途中自然遇怪，战斗结束事件2771到打开角色的已接受输入2793为3.3881432秒。Dispatch、Resume、Heal Entry、Begin、Open0正常入口段的ocr_calls全部为0；Begin932.3ms、Open0 629.6ms。恢复尝试已关闭，Heal Terminal事件2847后继续返城。Recover@await约4.99秒，包含转场期间6次OCR调用，不声称所有恢复页面/未知页面都没有OCR。

这是本候选的真实战后入口，不是宝箱后的同口径样本：本次战斗没有出现宝箱，不能直接将旧“奖励继续→打开角色25.1秒”与新“战斗结束→打开角色3.39秒”比较为同条件加速。宝箱后的对应转场继续从自然循环收集，不额外制造战斗/宝箱或重复启动任务。原始事件、动作时间及截图在正式runs/DC9A8D75-7AD8-4CE4-A3AE-B2B87CE6A854/1。

收尾检查时新批次已成功1/100，run2自动Running，reason/storage_error为空。不是全100轮通过。

日志与原最近240帧/128MiB策略沿用，不新增系统级内存跟踪。RESOURCE_UNRESOLVED及原RA验收缺口保留。本轮未commit/push。
