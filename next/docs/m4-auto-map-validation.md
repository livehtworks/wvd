# M4 自动移动后的地图确认

## 漏项与修正

固定旧源 `StateDungeon.startAuto` 在 `mark_auto`/`chest_auto` 移动停止后，继续执行 `StateMapSearch`；宝箱自动按钮不可用也进入这段。此前新版仅把 StoppedExit/UnavailableExit 交回 Dispatch，再次选择同一自动按钮，没有承接地图上的目标确认。

现在自动子图停止后转入已有 `reach_map_target`。地图子图重新截图、开图、搜索并检查目标中心，不沿用自动按钮坐标。`stay` 和退层 `dungFlag` 的规则不变；自动无目标提示仍是独立的正常结束条件。父路线确认接受“地图图标存在且中心已改变”的旧到达证据，但不存在图标、仅停止或开图失败都不算完成。

## 当前状态

首次构建通过，状态组 19 方法通过（5.640 秒，`m4-trap-state.log`）。地图流程首轮 3 方法均失败（92.761 秒，`m4-auto-map-workflow.log`，私有根 `m4-workflow-u5iw2ik4`）：均只发出一次正确 Auto 点击，在新帧后置检查超过 2 秒 TTL 时被 `SCENE_UNCONFIRMED` 拒绝，未到开图阶段。因方法内首个断言失败，停止子例尚未运行，不能写成四场景全部测试。

事件记录中宝箱按钮后 frame 18 的识别耗时约 2075.7ms。新增 `auto_route_post` 纯视觉分类，保持原 `moving|encounter|outside|no_target` 的逻辑结果：无地图且有地下城标记已经足以返回已知状态；否则继续查遭遇/无目标/退场。它不授权后续输入，不更改通用 any/all 错误传播，不放宽 TTL；实际访问探针的 Error 仍失败。资源通过同一探针定义进入发布清单。新增 16 种布局在 direct/Pipeline 比较新旧布尔结果，尚待运行。

修正后需验证自动标记停止、不可用宝箱按钮、停止/开图拒绝及受影响路线回归，再记录最终证据。首次失败保留，不计作业务通过。

不包含整个要塞任务，也没有实机输入；保护旧 src/config/mod/dist。
