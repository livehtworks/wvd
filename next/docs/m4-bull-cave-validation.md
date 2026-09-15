# M4 牛洞与因果调整

固定旧源`6585f407`。牛洞完整链在最后允许的定点修正后仍失败，当前为
**BLOCKED / 完整任务未通过**；保留各轮失败，不再改代码或复测本链。
最后一轮仅运行ACTIVE_REST=false，true分支因同一测试方法前项断言失败而未触达。

## 牛洞

- `tasks/bull_cave`编译有限图，`quests/BullCaveCycle`独占周期/阶段/跳跃意图。
  第一段跳跃、要塞、王城、住宿与指定任务；不休息时第二段连跑三个位置后退出；
  休息时第二段只跑一牛并实际住宿，第三段再跑其余两个位置并退出。
- 路线保持三个旧坐标/方向；LBC_quit单独退出，不能以本内地图计作回城。
  每次领取访问和中途住宿有独立付款回执，不能将领取前的住宿当作一牛后的住宿。
- 跳跃pending、领取pending或付款pending时不自动重放；RunCoordinator仍是唯一续段所有者。

## 因果

- `navigation/time_leap`共享原有无因果编译实现，不引入另一套执行器。
  `ACTIVE_CSC=false`使用原无因果路径；开启时仅慢路径在跳跃前执行设置，
  可见目标快路径成功仍按旧源码直接退出，不强行补做因果。
- `navigation/causality`先红通道2倍检查didnottakethequest并向上滚动，再按指定
  RGB倍数选EnaWasSaved并向下滚动，最后Android Back返回leap。
- 纯视觉只在当前Session保存固定`[77,349,757,1068]`BGR副本，比较阈值严格`<0.006`。
  参考节点才更新副本，比较节点只读，防止同页候选重查把滚动错误判成完成。
  同一帧不允许被当作滚动后新帧；副本不是业务状态/持久化数据或输入许可。
- 有界32次滚动；CSC页面标记丢失时不继续盲滚，仍需验证真实页面滚动中标记稳定性。

未完成：两类牛洞完整路线与连续周期、完整付款/跳跃及配置/mod/恢复矩阵。
因果分项证据见下文，不能代替牛洞完整任务验收；本链不继续循环修正或复测。

## 首轮问题

状态35方法组通过；独立计划定位ACTIVE_REST=false可编译、true报TASK_TARGET_ARGUMENTS。
单目标路线的大括号json构造改为显式J::array，保留路线数组层，不修改TaskPlan参数契约。
`m4-workflow-4o72cdln/bull-cave-full-False`在19输入后POSTCONDITION_TIMEOUT，
没有命令不匹配：世界跳转要求dungFlag/openworldmap，夹具直接给mapFlag，跳过打开地图。
夹具改为真实入口页再点击777/150打开地图，预期完整输入相应37/47；生产世界地图条件不放宽。
修正后的`m4-workflow-7g_0bqno/bull-cave-full-False`前进至28输入，在第二个路线点
打开地图前Failed/SCENE_UNCONFIRMED，无错序。输入前frame86捕获至场景识别返回
约1968毫秒，随后相同目标确认又耗约62毫秒，超过2000毫秒门禁；两项均Hit并不许可过期输入。
本轮停止/拒绝各1输入通过并保留leap_pending；ACTIVE_REST=true完整分支因前例失败未执行。
上述三场景已核对EXE身份和静止。其后进行最后允许的定点修正：非并行父节点内的
纯视觉子树同步分片，不延长TTL、不删阻塞反证。最后结果仍失败，见下节，保持阻断。
因果复验`m4-workflow-gqgh3uz1`六方法通过：基础设置6输入、快路径2输入、
ROI变化后再次滚动5输入、完整慢路径19输入、拒绝/停止各1输入、未知后置Failed/1。
缺少symbolofalliance在发布前拒绝，连接/输入均0。七个实际执行场景已核对EXE身份与静止；
原生与配置参数均未跳过因果步骤。这些是因果分项，不是牛洞任务恢复矩阵通过。

## 最后允许修正后的失败

只读核验对象：`next/.local/m4-workflow-a5by2949/bull-cave-full-False`。
`execution.json`记录exit=0及EXE SHA256：
`570172a2123c8011ca34745ab69500210769d61e419305cef701f0b6c11318ec`。
核验时`next/build/m4/Release/test_m4_workflow.exe`实际hash与之相同。
exit=0只表示原生夹具成功输出结果，不表示业务通过；
`next/.local/logs/m4-extension-workflow-and-matrix-first.log`中牛洞方法仍为FAIL。

- Run为`897D1337-C0FC-45C5-ACDD-3F74C1393F93/1`；实际result.json为
  `Failed / BUSINESS_CONFIRMATION_STALE`，generation=2，quiescent=true、result_saved=true，
  storage_error为空、secondary_errors为空；业务摘要与output.json.snapshot一致。
- 完成业务单元=1：第一段17次后端输入、检查点有效；第二段8次后端输入、未达检查点。
  全Run accepted/backend_called=25，attempted=27、rejected=2、cleanup_called=0；
  cursor=25，对应预期37条输入，mismatch=false。不能写成零拒绝或完整路线成功。
- bull_cave仍active=true、phase=5、completed_cycles=0、rest_enabled=false；
  生命周期调用为空。这不是前轮的28输入SCENE_UNCONFIRMED，也不是命令错序。

首因定位在第二代次`FirstDungeon_Confirm0`的实际新帧业务确认：

| 事件seq | 事实 | 单调时间ns / 距本帧捕获 |
| --- | --- | --- |
| 2301 | custom.enter，节点FirstDungeon_Confirm0，depth=1 | 414132829967200 |
| 2302 | frame.captured，第二代次frame_id=65 | 414132830060600 / 0ms |
| 2305 | recognition.custom返回Hit | 414134844293300 / 2014.2327ms |
| 2307 | 原生RecognitionNode.Succeeded返回 | 414134846972600 / 2016.9120ms |
| 2308 | FirstDungeon_Confirm0的Action.Failed | 414134847960500 / 2017.8999ms |

冻结run.json的max_frame_age_ms=2000。仅截图事件到Hit回执已超过2秒；
结合Session/Run保存的BUSINESS_CONFIRMATION_STALE，可定位为确认帧龄保护拒绝，
不能把Hit等同于当前有效观察。表中是事件单调时间，不伪造精确墙钟异常时间。

后续2310的child.result为valid=true/status=3000、2313为Tasker.Task.Succeeded，
均未覆盖业务首因。2316/2317是Inactive(kind=14)尝试被INPUT_CLOSED拒绝；
2318至2320依次销毁tasker/controller/resource，2321才记录session.quiescent，
2322的已提交run.terminal保留Failed/BUSINESS_CONFIRMATION_STALE。
events.json仅有有界256条且resync_required=true；result.json终态事件last_seq=2322，
不能声称保存了完整原始事件历史。

该方法按False、True顺序执行，在False的Completed断言处失败退出；本证据根没有
`bull-cave-full-True`目录。因此本轮口径为**False已运行且FAIL，True未触达/NOT_RUN**，
不是两分支都失败或都已验证。按用户明确边界停止本链修正/复测，保留BLOCKED。
