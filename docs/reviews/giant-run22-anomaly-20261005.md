# 巨人 run22 异常核查

## 范围与现场

用户要求“再看一下异常”。本次只读取源码、API、历史日志，并通过正式设备接口截图；未点击游戏、未启动循环、未改程序或配置。

- 服务：candidate107，17654；run namespace `845DFCB2-AED3-4D7E-A03F-60139292C275`。
- 当前批次请求 `1aacf56e-250e-4b26-a75a-9721e047f6d5`，目标100，完成18，repeat failed/inactive。
- run22：Failed/quiescent，无待确认输入；`FLOW_INVOCATION_TIMEOUT:Task_FirstDungeon_Entry`，1377.596秒，结果文件本地时间2026-10-05 04:38:09。
- 设备连接、正式截图成功；当前游戏仍在迷宫。没有本轮程序崩溃证据，业务crashes=0，不把任务超时称作闪退。

## 已确认链路

1. 业务结算2场战斗、1次开箱，战斗累计306.234秒；task_step仍0，悬赏阶段仍在副本段。
2. 留存详细事件覆盖最后470.2秒。`Route0_StoppedExit`至少23次，经`Dispatch -> Resume -> Heal -> SelectPoint -> CallRoute0`重新导航；每次公共继续导航输入被确认，但不代表人物位置推进。
3. 最早剩余帧f672（04:27:07）与失败帧f938（04:38:07附近）处于同一小地图位置。继续箭头为白色，不能用此前黑色禁用按钮规则解释。
4. 父迷宫声明400秒加开箱900秒累计期限。到期由执行器直接fail；最后continuous_exception=false，因此60秒异常重启未触发。

## 源码缺口

### 后续核查：找不到目的地路线提示没有接入繁中

用户明确补充现场点击后出现找不到前往目的地路线的提示。本次继续只读核对整条调用链，未发送游戏输入。

- 巨人任务数据正确：`legacy-quests.json`的`GiantBounty._TARGETINFOLIST`为`mark_auto -> dungFlag`，编译追加目标战后等待3秒和末点哈肯到达确认。不是用户把返程步骤排错。
- `auto_route.cpp`的Done本来识别`NoChestCanBeFound`或`theRouteToTheDestinationCannotBeFound`后正常返回；上层应Confirm当前点并推进下一点返哈肯。
- 两个模板在`semantic-assets.json.template_languages.en`中明确属于英文；`locale_assets.cpp`没有它们的繁中语义映射。繁中运行的`native_recognizers.cpp`在加载/匹配之前直接返回`template_language_excluded`。因此此结束条件在当前语言下不可达，不是ROI相似度不够。
- 正式17654接口`/api/v1/recognition/probe`以`resource_locale=zh-Hant`和原条件复核，返回`NoHit`、`evidence.reason=template_language_excluded`。该检查使用当前无提示截图，仅证明语言门禁先行排除，不宣称验证了提示正例。
- 正式保存公共流程`navigation-resume` revision `380ce233873309e244faa16faf3bbe9f69bf37cf9d0253d63501f13e0ddf549c`的点击后置仍为`auto_route_post`。该识别先见`!map && dungFlag`就返回stage=dungeon，不读取、保存无路线提示结果；上层Done随后重新读帧。即使补繁中配方，短暂提示也可能在后续识别前消失，需要在动作后首帧保留终点证据，不可把“仍是迷宫”当位移成功。
- 当前Done未命中后，白色继续按钮不满足黑色Arrived规则，小地图静止只交付stopped，上层又回Dispatch并点mark_auto，最终重复至总超时。失败不要求用户调整任务顺序。
- 旧`src/script.py`的startAuto在点击后立即连续检查两帧无路线/无宝箱提示，命中调用TargetPointComplete并退出自动搜索；另一个旧流程的注释明确指出无路线提示不是持续性的。

修复契约：对此巨人标记步骤，确认无路线提示后终止当前导航目标并进入返哈肯、返城及悬赏后续，不是把整个运行器标成Failed；不得再次进入同一个标记步骤。其它导航用途仍按各自终点语义处理，不能把返哈肯失败提示等同于已到哈肯。此前目标战斗/宝箱承接缺口仍存在，但不应替代此处已明确证明的语言接线缺失。

- `navigation/auto_route.cpp`将小地图静止交付stopped；`tasks/dungeon_route.cpp`对带战后等待的标记路线将stopped重新交回Dispatch。无跨重入的同位置未进展判据，重复进入子流程和确认按钮能形成正常路径循环。
- `FlowExecutor`已知移动等待会清除异常窗口，且停止后存在可命中的正常后继。因此局部无进展不自动成为连续异常，最终只能由父调用累计期限终止。
- 目标战斗`FightTarget`的chest出口直接去Dispatch；普通Fight成功也回Dispatch。只有FightTarget正常成功返回才进入AfterTargetBattle/Confirm。战斗结束直接出现宝箱可能绕过目标完成路径，这是确定存在的代码缺口；**本轮是否具体走过此分支尚不能证明**。
- 修复应承接目标遭遇在宝箱/战后恢复间的状态，并跨导航子调用保留真实位移进度；不能通过单纯放大总超时、把白色箭头当完成，或把任意战斗计数当目标击杀来掩盖问题。

## 日志与资源

- `execution-events.jsonl`、`action-timing.jsonl`、`diagnostics.jsonl`均为0字节；result仅有1024条尾部事件加终态。战斗/开箱交接事件已丢失，不能还原第一处目标进度丢失的精确路径。日志落盘问题需独立修复，不能声称完整追溯。
- 失败时进程私有提交831.2 MiB，工作集782.5 MiB，识别缓存2.97 MiB，系统提交87.6%。本轮终态是显式流程超时，没有OOM错误；单点数值不证明无泄漏，也不足以将此次停止归因内存。

## 证据

- `next/.local/c11-flow-product/data/runs/845DFCB2-AED3-4D7E-A03F-60139292C275/22/result.json`
- 同目录`diagnostics/1.png`、`recognition-memory.log`。
- 当前只读截图：`next/.local/giant22-current-anomaly.png`。
- 历史环形截图：`next/.local/c11-flow-product/data/recent-frames/20261004T202707680_r22_f672.png`（可能随后续运行滚动清除）。

本次为诊断，不宣称上述缺口已修复、部署或实机通过。
