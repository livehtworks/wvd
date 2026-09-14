# M4 子流程普通中断

## 实现

`PipelineCompiler::interrupt_on` 在发布前把本构建器声明的节点编译为有限中断图，不增加运行器或脚本 VM。下一候选优先检测阻塞；每个输入场景以及 WvdConfirm/WvdCombat 的实际新帧确认排除阻塞覆盖层，不能因覆盖层后仍可见完成锚点就消费技能或推进任务点。

中断退出命名为 BlockedExit。独立子图通过 RequireRecovery 明确要求外层处理；组合时调用者显式绑定为正常子返回，再进入外层分派、Common 子图和重新识别。仅处理自己声明的节点，不跨越 append/define_child 的所有权边界，不改 Common 处理器，不重置父预算或业务状态。真正 on_error 不变，底层失败和识别 Error 不转换为普通返回。

已接入地图、自动寻路、Auto、角色回合、遭遇、宝箱和角色恢复；路线明确绑定这些出口。角色面板恢复中被网络弹窗打断后，可从现有 trait/recover 面板继续，而不是再次点击地下城角色坐标。

固定坐标动作的场景和目标请求保持完全相同，复用现有 Gateway 同帧结果；位置模板目标仍保留原中心许可。没有扩张 TTL、降低阈值、新增跨帧缓存或让布尔匹配生成坐标。

## 验证

首轮 m4-interruption-tests.log：5 方法中 4 通过、1 失败，205.289 秒。角色恢复因包装后固定点位场景/目标重复求值，在第一个输入前得到 SCENE_UNCONFIRMED，后端调用为零。证据保留在 m4-workflow-_9i3_tbd；不把这次门禁拒绝改为成功。

修正固定点位请求复用后，m4-interruption-fixed-scene-build.log 构建通过；5 方法/6 场景全部通过，236.737 秒，证据 m4-workflow-tqc9mrsv：

- 地图选点后出现 Retry，不发送旧 AutoMove；关闭后以新光标确认任务点。
- 开技能面板后出现 Retry，即使 dungFlag 可见，也不消费准备中的技能。
- 开箱后的 Retry 返回选择角色，最终只计一个宝箱。
- 打开角色面板后出现 Retry，在原面板继续，恢复序号为一。
- Stop 与底层拒绝均不发后续点击，不推进任务点。

三个组合成功场景均为 generation=1，无生命周期调用；输入分别为 2、5、6。保护基线 450 文件无改变。

阶段提交 `6e24743` 后完成共用子图回归：64 方法，63 通过、1 失败，2110.580 秒。日志 `m4-interruption-shared-regression.log`，私有证据 `m4-workflow-0xyz2kz7`，EXE SHA256 `f71f52baf1fad11d9f0b0d185db5c8da74acc181cc89818bf89a41db66a0a7ce`。该产物在回归期间保持不变；后续源码不混入这一结果。

唯一失败 `heal-rotate-seek` 在第六次合法输入后触发 SESSION_TIME_LIMIT，无错点。恢复子图继承的 60 秒默认不足以承载旧 31 次、每次 2 秒的尝试。后继阶段设置明确的有限总预算，修复后复验单列于 `m4-effect-interruption-validation.md`；不把本次失败改写为通过。

## 未完成边界

这里只证明列出的普通插入和新帧确认。入城/住宿/专项任务中途插入尚未统一；实际副作用发生但确认缺失的窗口仍需要 operation_id/业务回执对账，不能宣称已实现全业务 exactly-once。死亡、复活、Pause、全局对话和 15 项专项继续推进。完整任务通过数仍 0/58，真实质量、资源/性能和任意原生阻塞取消仍未放行。
