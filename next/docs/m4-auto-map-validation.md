# M4 自动移动后的地图确认

## 漏项与修正

固定旧源 `StateDungeon.startAuto` 在 `mark_auto`/`chest_auto` 移动停止后，继续执行 `StateMapSearch`；宝箱自动按钮不可用也进入这段。此前新版仅把 StoppedExit/UnavailableExit 交回 Dispatch，再次选择同一自动按钮，没有承接地图上的目标确认。

现在自动子图停止后转入已有 `reach_map_target`。地图子图重新截图、开图、搜索并检查目标中心，不沿用自动按钮坐标。`stay` 和退层 `dungFlag` 的规则不变；自动无目标提示仍是独立的正常结束条件。父路线确认接受“地图图标存在且中心已改变”的旧到达证据，但不存在图标、仅停止或开图失败都不算完成。

## 当前状态

首次构建通过，状态组 19 方法通过（5.640 秒，`m4-trap-state.log`）。地图流程首轮 3 方法均失败（92.761 秒，`m4-auto-map-workflow.log`，私有根 `m4-workflow-u5iw2ik4`）：均只发出一次正确 Auto 点击，在新帧后置检查超过 2 秒 TTL 时被 `SCENE_UNCONFIRMED` 拒绝，未到开图阶段。因方法内首个断言失败，停止子例尚未运行，不能写成四场景全部测试。

事件记录中宝箱按钮后 frame 18 的识别耗时约 2075.7ms。新增 `auto_route_post` 纯视觉分类，保持原 `moving|encounter|outside|no_target` 的逻辑结果：无地图且有地下城标记已经足以返回已知状态；否则继续查遭遇/无目标/退场。它不授权后续输入，不更改通用 any/all 错误传播，不放宽 TTL；实际访问探针的 Error 仍失败。资源通过同一探针定义进入发布清单。新增 16 种布局在 direct/Pipeline 比较新旧布尔结果，尚待运行。

第一轮修正检查点 `e9bf8d5`：16 种布局在 direct/Pipeline 与原布尔式对照均通过（1 方法，77.095 秒，`m4-auto-post-layouts.log`，私有根 `m3-fixes-mr5gmwd7`）。但流程复测及自动子图回归 6 方法仍全部失败（173.352 秒，`m4-auto-map-workflow-retry.log`，`m4-workflow-2iks2ms4`），这次六个已执行场景都在输入前零点击失败。宝箱用例 frame 17 到 scene 识别约 2744.2ms，说明只收敛后置还不够；方法内后续子例未执行。

一次有限诊断仍按预期复现零输入失败（`m4-auto-scene-diagnostic.log`，`m4-workflow-35xv2j8d`），没有逻辑修正或性能窗口重测。scene frame 17 耗时 2220.5ms，保留的 18 份去重匹配记录中 OpenCV 匹配合计 1059.1ms、非有限分数处理 36.9ms、归约 12.7ms；阻塞分类未命中的细节没有全部保留，不能把 1059.1 当成所有模板总耗时。只读工具 `tools/inspect_recognition_timing.py` 可复算，默认仅展示最后八次 direct 观测。

第二轮修正：只在顶层、纯视觉白名单的 all/any 使用固定 OpenCV 两个同步分片。每路独占临时模板索引/memo，像素和封存资源只读，合流后按原顺序传播全部错误并合并资源缓存；business、movement_stopped、历史特征和未知模式不并行。条件深度仍受 8 层限制，不创建执行器、不更改全局线程数、不 detach。新增诊断标识区别实际求值路径。正在验证原有组合错误、门禁/ROI 和因果流程；未通过前不宣布修复。瞬时资源成本与长期资源稳定性仍不外推为通过。若仍失败按两轮边界保留阻断，不循环挑通过。

不包含整个要塞任务，也没有实机输入；保护旧 src/config/mod/dist。
