# M4 队友死亡提示

## 范围与旧语义

固定旧 `Factory.IdentifyState` 先检查 Dungeon/Chest/Map/Combat，再检查 `someonedead`；命中后重置战斗策略，在中心 100x100 区域连点五次。它不是 `RiseAgainReset`，不应取消失败遭遇或记一次全队复活。

新版 `party_death` 纯视觉条件先检测模板，再排除已知正常场景、Pause 和角色/技能详情。它作为通用阻塞候选，交给独立 `recovery::dismiss_party_death` 子图消费；WVD 状态只记录提示编号/待处理状态，输入仍在 GuardedAction。

确认提示后只重置一次策略；最多五次中心区域输入，每次间隔一秒且重新核对提示。已回正常场景则立即清除回执并返回，五次仍无效报告 `party.death_prompt_unchanged`。普通 Retry 插入返回后补齐清除回执，不重发旧点击；停止/底层拒绝不写清除，不记复活或成功战斗。

这是明确的安全性差异：不保留旧版提示消失后仍继续连点的行为；固定中心点属于旧随机区域，不声称保留旧随机序列。正常场景排除比旧 `someonedead` 分支更保守，以避免王城/地图骷髅误触发。

## 验证

实现已接入通用阻塞和正常 Farm 迭代。七个相关原生目标构建通过，状态层 15 方法通过（5.149 秒），包括提示重复观察只重置一次策略、跨段保留待处理状态、清除回执去重，且不冒称复活/胜场。

首轮原生流程 5 方法中 3 通过、2 失败（127.122 秒）。首个点击清除、正常场景负例、Retry 及原任务续接通过；第五次清除与持续提示在中途被 `SCENE_UNCONFIRMED` 拒绝。证据 `m4-workflow-hqiod85p`。

根因由逐事件时间定位：后置帧 captured 到 Hit 已耗时 2244.30ms，超过原 2000ms TTL，不是点击错误。原 `any(boot_post, RiseAgain)` 会把死亡页已做过的就绪排除和无关启动候选重算。修正为专用有序 `party_death_post`，并按旧状态优先级检查必要场景，再排除普通覆盖层；没有改变通用 any/all 的 Error 传播契约或帧有效期。

七目标修后构建通过；同例补验和父预算测试 6 方法/16 场景全部通过（329.929 秒），其中死亡提示 5 方法/12 场景。持续无效实际五次点击后以 `party.death_prompt_unchanged` 返回，49.654 秒；停止/输入拒绝各一次输入后正确终止。没有重试挑选结果或改变原断言。

证据根 `m4-workflow-wsi2pj6e`，流程 EXE SHA256 `8a49ccd0c7664fbf16b2ca299abe38f27f05af6bad3446e81cc633eff9851904`。构建和测试日志 `m4-party-death-bounded-retry-build.log`、`m4-party-death-retry-workflow.log` 保留在私有目录。

同产物通用阻塞与 Pause 回归 12 方法通过（214.439 秒），证据 `m4-workflow-gao8psst`，日志 `m4-party-death-common-pause.log`。实现提交 `2754096`；多人死亡后续改动未混入该产物。完整任务和真实质量尚未验。

`multipeopledead`/SUICIDE、善恶写回、专项对话仍属于独立未完成范围；此报告不把一个提示处理当作全部死亡或全局事件完成。
