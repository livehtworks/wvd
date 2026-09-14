# M4 入本与自动寻路验证

## 实现范围

`navigation/dungeon_entry.cpp` 把固定任务 `_EOT` 编译成顺序 Maa 图：可选前置页、世界地图、目的地、GotoDung 和入本新帧确认。fallback 数组是明确先后关系，不能转成候选列表。固定坐标动作需要已知选择页，模板点击需要目的地正证据；都排除已入本和世界地图。最后一次点击成功不等于入本。

`navigation/auto_route.cpp` 区分自动宝箱、标记、撤退和 stay。按钮不可用、遭遇战斗和停止移动分别退出，不能假记任务点完成；只有明确的无目标提示或已出城条件才到该子流程终点。已有 `Mark_auto` 大小写沿用旧源码实际分支，不擅自规范成 `mark_auto`。

## 验证与失败记录

- 最终 `m4-entry-local-scene-tests.log`：7 方法、12 个真实 Maa 离线因果场景通过；证据 `next/.local/m4-workflow-nvipbgbx/`。
- 最终 `m4-entry-final-plan-tests.log`：4 方法通过；43 个 dungeon 的入本图都能编译且引用资源存在；58 项原字段保留。证据 `next/.local/m4-plan-bvk4hrax/`。
- `m4-entry-state-tests.log`：9 个状态方法通过。`m4-entry-memo-m3-tests.log`：3 个 ROI/组合/门禁定点回归通过。
- 首轮自动寻路曾因同帧复合条件的重复求值超过 2 秒而触发 SCENE_UNCONFIRMED。第一次修正：仅在一次 evaluate 调用内缓存完全相同的子表达式；不跨帧/代次，不短路隐藏 Error。
- 扩展回归中的 `entry-world` 又在 3 次实际输入后触发 STALE_ACTION_INTENT：识别用时约 1.86 秒，SDK 动作排队/默认等待约 0.21 秒。第二次修正：点击已识别目的地不再重查整条路线的全部候选页，仅保留目标及必要反证；固定坐标 fallback 仍保留已知页证明。未增加帧有效期，也没有绕过最终门禁。
- 首轮 `m4-entry-auto-tests.log`、扩展失败 `m4-entry-final-workflow-tests.log` 和相应私有目录原样保留。扩展组中的 13 个技能场景全部通过，不把失败组包装为整体通过。

所有图像都是隔离合成图，截图不推动剧情，实际 SDK、输入门禁和原生业务未替换。每例保存 EXE/SDK 身份、输入序列、发布图及原子结果。

## 剩余边界

43 项仅证明入本图编译，不是完整任务执行；12 场景也不等于 43 任务全部运行。全部 58 项完整任务执行通过仍为 0。下一步继续外层任务推进、完整战斗/补给/恢复、15 个专项及确认写回，保留资源、真实视觉和设备发现未决项，不进入 M5。
