# 巨人第4轮停止调查

## 现场与结果

- candidate105/PID33168仍存活。批次`giant-rescue105-new50-20261005`完成3/50，run4 Failed/quiescent，repeat.active=false。不是成功跑完。
- 原始证据目录：`next/.local/c11-flow-product/data/runs/D6C283C6-AA29-4E38-A4E6-D7DFC788C4C6/4/`，完整结果为`result.json`。
- 新鲜只读截图`next/.local/giant-stopped-current-20261005.png`为繁中“請注意”启动页；不是救人漩涡。没有输入游戏、修改方案、重启或恢复循环。

## 已确认链条

1. 正式巨人方案中面具为左上技能2级、频次“重复”。最后一次prepare事件2206正确识别面具0.97350955；不是ROI认错人物。
2. 该战斗有四个面具技能成功段。第四次详情原图`recent-frames/20261004T163803980_r4_f225.png`明确显示“時滯錯覺Lv2，SP48→8，消耗SP40”。
3. 下一回合仍尝试打开相同技能。末尾输入坐标[266,965]正确位于左上技能；f287/f288及diagnostics/1.png仍是普通菜单、SP8/166，没有实际打开技能详情。未接入繁中的notenoughsp/notenoughmp旧资源分支；没有留存明确不足弹窗，不把缺失弹窗冒称已识别。
4. 事件2452/2453为输入后置未确认及FLOW_STAGE_TIMEOUT；2454记录CONTINUOUS_EXCEPTION_TIMEOUT，2455记录程序主动重启应用。之后ADB_TRANSPORT_FAILED、实例重拉，再次观察耗时51.995秒。模拟器退出的底层原因未归因，不能统称自然闪退。
5. 游戏重新拉起并压入ReconnectBoot处理器后，旧combat-open-detail累计期限已耗尽，`FlowExecutor::tick()`仍遍历全部调用栈直接fail。最终错误为FLOW_INVOCATION_TIMEOUT，而不是Boot正常完成或救人失败。

## 同时发现

- 上轮再起繁中OCR经RiseAgain替换进入正常战斗any/all后置；该复合识别会检查所有子项，不因普通战斗已经成立而跳过OCR。这是新接线的范围问题，应改为限定复活页面的识别，不能靠扩大等待期限掩盖。
- 本次战斗会话521.37秒，其中recognition_other264.23秒。该桶并非专用OCR计时，不能将264.23秒全部写成OCR耗时。
- 会话活跃私有提交约856.57MiB，释放识别会话后约259.14MiB，没有此次内存不足崩溃证据；OCR引擎及工作区的具体占比未独立归因。

## 修复与实跑

- candidate107部署到17654，服务PID46400、实例`8240CB5E-E2A3-4797-A53B-8A17020400FD`。正式请求`giant-repair107-remaining47-20261005`继续剩余47次；原3次成功保留。本轮不修改用户方案、不提交/push。
- `combat-open-detail`给正常网络/补点留20秒；到期后不是停根任务，而是新帧确认同角色原菜单再交`UnavailableExit`。手动防御须再确认角色推进；回执为`defend_fallback_confirmed`，不是技能成功，不删除未施放行/清组/启动Auto。真正识别到SP/MP不足提示先重试1级，再交防御。未见不足弹窗时日志只称菜单无进展，不能反推已识别不足文字。
- 源公共定义及正式保存定义已同步，原revision`6652d095...`，新revision`496d43e7b0f86eb258792ded7eb3c90fa9851ae8e62f5486e8b4328e67c077e5`。备份`next/.local/giant106-workflow-backup/combat-open-detail.json`。仅修改打开详情的预算、失败边及手动防御handoff，其他正式流程不覆盖。
- FlowExecutor不检查被活动事件挂起的父调用期限；恢复后显式Replan先于旧期限检查。WVD重启事件在Boot确认后回根Entry重新认页，清失效子栈但不清账目/重放未确认技能。全局截止与处理器自身预算仍保留。
- 再起观察先检查正常战斗；定位再起仍为原精确唯一OCR。资源提示探针排除正常菜单/详情/无NEXT动作动画。正常菜单f287以及真实防御动画`C0F36779-F59D-431F-91E8-BE31836B084E/1/diagnostics/1.png`的正式probe均为NoHit、`ocr_skipped=true`，不再进入OCR。
- 定向生产执行器检查：打开详情同页补点、变化后不再点击、同页无进展转手动防御；防御结算保留技能行/推进epoch；过期子调用恢复/正常进展不重启/取消；受保护输入不重放。日志位于`next/.local/giant106-*-check.log`、`giant107-open-check.log`。这些不是实机整轮通过。
- candidate106真实续接已通过启动页返回原巨人战斗，面具SP8；补点10次/20秒后no_progress，确实输入手动防御。新增资源错误OCR漏传`mode=ocr`，在防御动画误进入OCR后报`CUSTOM_OCR_CONTEXT_INVALID`而停止；该失败保留，不计成功。candidate107修正上下文参数并以无NEXT排除动作动画，再开始剩余47轮。
- candidate107实跑命名空间`845DFCB2-AED3-4D7E-A03F-60139292C275`/run1：事件521记录20秒窗口内10次补点后的no_progress，事件544为`defend_fallback_confirmed`，action_confirmed=true、skill_confirmed=false、consumed=false。后续爱丽丝与狮樱success回执均存在，已推进柚奈；循环Running/active，当前新增成功仍0/47，不扩大为整轮验收。截取证据`next/.local/giant107-live-summary.json`，原始事件随正式RunStore留存。

## 验收边界

- 真实再起两种页面、真实繁中资源不足弹窗正例及重启现场重新规划还未自然验证；不制造死亡/断网来补数据。
- 47轮还在实跑，不称50轮已通过。内存增长具体来源和模拟器此前退出底层原因仍未归因。
