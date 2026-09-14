# M4 地图目标与子流程组合

## 实现范围

- `PipelineCompiler::append` 将已有有限图按前缀内联，重写所有后继和错误边；子终点改为调用者的后继，不再触发根终点。重复名称、未知边、未声明资源和权限仍在发布前拒绝。
- `navigation/map_route.*` 承接普通地图搜索、滑动顺序、主 ROI 与排除区、固定地点与楼梯完成判定、选点和 Auto-Move。位置目标的 `reached` 与楼梯 `through_stair` 为 Hit 才允许完成。
- 每次动作重新取场景与目标。选点后若出现战斗/宝箱，结束地图段并明确请求外层分派；不继续 Auto-Move，也不提交任务点。
- 移动停止比较保持旧 `StateMoving_CheckStop` 的小地图 `[650,25,225,225]`、3 秒采样间隔和灰度差 `<0.1`。缓存仅保留本 Session 的一个灰度样本，重新开地图后还须核对完成条件；这不是游戏卡死判定。
- Auto-Move 点击后等待 3 秒再识别持续提示，沿用原提示 ROI 和 0.35 阈值；明确记录 `navigation.automove_physics_frozen`。错误楼层、缺失目标及预算耗尽保留独立原因。
- 原始对照：固定旧源码 `StateMap_FindSwipeClick` 2862、`StateMoving_CheckStop` 2907、`StateMapSearch` 2941。

## 验证证据

- 构建日志：`next/.local/logs/m4-navigation-build.log`、`m4-navigation-fixture-build.log`。
- 首轮 `m4-navigation-tests.log` 执行 18 个方法，13 个通过；5 个地图方法在解析阶段因测试任务漏填必需 `_EOT` 被拒绝，日志原样保留。
- 补齐测试任务必需字段，不修改计划校验。定点复测 `m4-navigation-map-retest.log` 的 5 个方法全部通过，包含 9 次原生调用；实际证据在 `next/.local/m4-workflow-99pxka0x/`。
- 首轮证据 `next/.local/m4-workflow-q6yi747q/` 包括入城接住宿的完整成功与住宿输入拒绝。子入城成功不能使根任务提前 Completed。
- 离线设备仅在正确输入后变更场景，截图不会推动进度。移动用例实际等待间隔，重新开地图后才出现独立指定的光标样本；测试不是直接将视觉函数替换成 Hit。
- SDK、合成图、完整事件和原生输出保存在上述隔离目录；未操作真实设备或旧配置。

## 后续依赖

这只是地图目标有限段，不是完整 dungeon。遇敌交回外层的分派、自动目标、暴风雪和恢复补给、完整任务图仍须接入；不能把本段 Completed 算作 58 项任务通过。目标标记、Pause 的真实质量及资源未决项不因本轮改变。
