# M4 多人死亡提示

## 旧语义与实现

固定源 `_SUICIDE` 仅有声明、`IdentifyState` 在 multipeopledead 分支置 true、`RiseAgainReset` 复位三处引用，没有战斗读取方。因此新版只保存该事实，不新增所谓“自杀战斗”模式。

`party_defeat` 条件与单人提示共用正常场景/Pause/角色面板排除规则。两个提示同时出现时先处理 someonedead；多人提示只允许定位 skull 图标，不进行中心盲点。子图最多六次、每次二秒并重新确认提示；提示改变返回通用分派，仍未改变以 `party.defeat_prompt_unchanged` 请求有界恢复。

观察去重沿用稳定操作 ID，游戏重启保留旧 _SUICIDE 语义，真正复活才复位。关闭多人提示不记复活或胜场。该字段在新版也没有战斗输入消费者。

## 验证状态

代码接通通用阻塞分派，七目标构建通过。状态组 16 方法通过（5.114 秒，`m4-party-defeat-state-retry.log`）；原生流程正在验证，未计通过。测试覆盖多人提示到 RiseAgain、地图骷髅负例、停止/拒绝、连续六次无效以及状态标志去重/复位。

首次状态组 15 个方法失败于共享前置检查：测试给 `revival_observed` 与 `resurrected` 使用同一个操作名，稳定 ID 冲突，正确报 `BUSINESS_OPERATION_CONFLICT`。改为正式流程相同的独立操作命名后通过；未放宽回执一致性保护。原失败日志保留于本机。
