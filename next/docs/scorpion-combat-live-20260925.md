# 蝎女战斗实机记录（2026-09-25）

## 现场与动作

- 设备为当前配置的 MuMu 实例 2，画面为 900x1600 繁中蝎女战斗。战斗开局图保存在本机 `next/.local/scorpion-combat-current-20260925.png`。
- 倍速起初为白色。旧 `combatSpd.png` 含英文 `Spd`，在本帧不命中；人工只点一次 `(47,1050)` 后变黄色，前后画面分别保存在本机 `next/.local/scorpion-combat-current-20260925.png` 和 `next/.local/scorpion-combat-speed-on-20260925.png`。两张 41x36 图标模板已进入资源包，新的原生战斗节点仅在白色命中时点击，并以黄色命中确认。
- 只创建一次性的“完整战斗”作者流程 `live-scorpion-combat-20260925`，没有启动完整蝎女任务，也没有执行领奖、交付或下一场战斗。正式运行的资源语言是 `zh-Hant`，用户配置的默认方案是“法术+地裂”；特殊敌人双开关在真实配置中仍为关闭，故本次没有实证 A/B 方案切换。
- 首次提交流程在准备期以 `SEMANTIC_OBSERVATION_NOT_POSITIONAL:combat.auto.off` 失败，游戏输入为 0。固定坐标点击的 `target_recognition` 实为场景复核，不应以位置资源契约解析；修正并打包后第二次正式运行进入战斗。
- 第二次正式运行的事件确认 `combat_observed`。`combat.prepare` 未选中技能，转入 `CharAuto`，再经未知状态保底节点送出 1 次点击。输入计数为 attempted=1、accepted=1、backend_called=1、rejected=0。没有证据表明“法术+地裂”的配置技能实际释放。
- 以入战实帧对“法术+地裂”中六个唯一角色头像执行正式 `portrait` 探针，全部 `NoHit`，最高分约 0.515，低于 0.8 的选择阈值。画面当前行动角色为爱莉丝，而该方案列出的角色不含她；本帧转自动战斗符合现有配置，不据此判定头像算法整体失效。后续要测试配置技能，应在方案确实覆盖当前行动角色的自然战斗中进行。
- 点击后游戏画面回到迷宫，终态图保存在本机 `next/.local/scorpion-combat-final-dungeon.png`。角色 HP 有变化，不能仅凭画面推断具体伤害来源或奖励情况。

## 终态误判与修复

- 工具运行结果为 `Failed: combat.common_screen_requires_dispatch`，不是 `Completed`；结果在 `%LOCALAPPDATA%/WvdNext/runs/<实例>/1/result.json`，同目录 `recognition-memory.log` 留有资源诊断。
- 对终态实帧调用正式识别接口：`dungFlag=Hit`，而 `blocking_screen=Hit` 的内层命中是 `resume.png`，框 `(734,263,54,53)`、分数约 `0.986`。该图实际是迷宫小地图旁常驻的“继续移动”按钮，不是阻塞页。阻塞中断抢先截走了战斗完成分支。
- 已将 `resume` 从通用阻塞页探针中移除；移动及启动恢复中直接使用该模板的调用保持不变。新候选对同一终态实帧的正式探针结果为 `blocking_screen=NoHit`、`dungFlag=Hit`，命中框 `(799,257,67,61)`。
- 新候选对倍速前后实帧的正式探针交叉检查：白帧仅 `combat_speed_off_zh_hant=Hit`，黄帧仅 `combat_speed_on_zh_hant=Hit`；两者的命中框均为 `(28,1033,41,36)`。
- 本次已结束战斗不重打。只有后续自然出现的战斗，才可验证完整战斗流程根终态、黄色倍速自动点击、配置技能和特殊敌人 A/B 实战分流。
