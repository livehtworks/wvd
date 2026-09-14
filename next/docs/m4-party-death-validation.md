# M4 队友死亡提示

## 范围与旧语义

固定旧 `Factory.IdentifyState` 先检查 Dungeon/Chest/Map/Combat，再检查 `someonedead`；命中后重置战斗策略，在中心 100x100 区域连点五次。它不是 `RiseAgainReset`，不应取消失败遭遇或记一次全队复活。

新版 `party_death` 纯视觉条件先检测模板，再排除已知正常场景、Pause 和角色/技能详情。它作为通用阻塞候选，交给独立 `recovery::dismiss_party_death` 子图消费；WVD 状态只记录提示编号/待处理状态，输入仍在 GuardedAction。

确认提示后只重置一次策略；最多五次中心区域输入，每次间隔一秒且重新核对提示。已回正常场景则立即清除回执并返回，五次仍无效报告 `party.death_prompt_unchanged`。普通 Retry 插入返回后补齐清除回执，不重发旧点击；停止/底层拒绝不写清除，不记复活或成功战斗。

这是明确的安全性差异：不保留旧版提示消失后仍继续连点的行为；固定中心点属于旧随机区域，不声称保留旧随机序列。正常场景排除比旧 `someonedead` 分支更保守，以避免王城/地图骷髅误触发。

## 验证

实现已接入通用阻塞和正常 Farm 迭代。七个相关原生目标构建通过，状态层 15 方法通过（5.149 秒），包括提示重复观察只重置一次策略、跨段保留待处理状态、清除回执去重，且不冒称复活/胜场。

原生流程定点验证正在执行，流程尚未计通过。测试包括首个/第五个点击成功、地图/城市/角色负例、停止、底层拒绝、连续无效、Retry 中断和原任务续接。日志 `m4-party-death-integrity-build.log`、`m4-party-death-state.log`、`m4-party-death-workflow.log` 留在私有日志目录。

`multipeopledead`/SUICIDE、善恶写回、专项对话仍属于独立未完成范围；此报告不把一个提示处理当作全部死亡或全局事件完成。
