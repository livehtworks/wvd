# 单角色自动战斗与诊断补齐

## 范围

承接candidate66的36轮只读排查，修复单角色Auto过早关闭的源码风险，补齐执行事件和线程/批次内存边界。用户明确要求不开始新循环；本轮不连接或操作游戏、不制造实机异常、不清理配置或旧日志。没有授权新的commit/push。

## 生产改动

1. `games/wvd/combat/auto_combat.cpp`新增唯一的`single_actor_auto()`业务段，复用原开启/关闭详情/异常分派链。开灯后只观察，不反复切换；在Auto仍开启、指令菜单已退出且详情遮挡不存在时才关闭，关闭后核对按钮状态或战斗结束。动画期间Active可能消失，不以Active必须常亮阻塞关灯。未配置角色及技能失败保底两类调用统一承接；全自动整场战斗不受影响。不以“灯亮”或背景动画当作本角色攻击完成。
2. `NativeOperations`的prepare事件记录所有已计算头像分数、阈值、方案epoch/名称、选择条目和没有技能的原因，不新增一次识别、不改变技能消费策略。可据此区分头像匹配不足与策略自动/耗尽。
3. EventJournal通过唯一RunStore回调在UI环淘汰前写`execution-events.jsonl`。每轮64MiB上限，失败/丢弃明确计数；必需执行审计不随可选日志开关关闭。终态成功不提前写进事件文件，仍以原子保存的`result.json`为权威。没有新增线程、无界内存队列或第二套事件来源。
4. 协调器在drive结束后显式释放线程捕获的NativeRunDefinition及backend引用；Application在结算/续轮前join已完成worker。新增`worker_definition_released`、`worker_joined`、`batch_payloads_released`三个采样边界，依旧由唯一RunStore按内存开关与info级别保存。后终态证据放到`memory-lifecycle.json`，最多三个具名标量快照，不追加到已冻结行数的诊断文件。整批配置释放时不关闭游戏或模拟器。
5. 只读分析器优先读取有序磁盘事件，核对run/instance/seq、行数及终态序号；旧记录明确显示只有尾窗。独立读取后worker内存，不把worker_finishing当作释放后基线。数据职责同步到`next/docs/data-authority.md`。

## 验证范围

- 沿用现有原生测试入口，仅增加单角色自动战斗的受控时序检查、磁盘事件跨环淘汰检查，并扩充既有内存边界检查。输入时序使用生产业务图和执行器、受控识别叶子；不能冒充真实动画识别通过。
- 计时分析器8项隔离检查已通过，包括旧尾窗标记、新事件序号缺口和后worker内存读取。
- 首次原生构建通过，事件历史与内存边界检查通过；单角色时序已执行至完整turn装配时因检查入口未装配公共步骤库失败，已补与正式入口一致的PublicFlowLibrary装配，不修改生产契约绕过。
- 最终原生/Vue构建与四个定向原生入口已通过：`test_native_author --single-auto-progress`、`test_native_coordinator --event-history`、`--logging-policy`、`--logging-boundary`。最后一项同时核对真实协调器接入的磁盘事件序号和三个内存快照。首次扩充落盘解析又发现旧协调器测试夹具使用`test/finish`而不是结构化来源路径，已修正夹具并通过，不修改正式日志或降低校验。
- 构建日志：`next/.local/logs/closure67-native-final.log`、`closure67-web.log`；打包和部署：`closure67-package.log`、`closure67-deploy.log`。差异空白检查通过。不运行旧全量矩阵。

## 部署结果

- candidate67已通过正式管理链部署到原`http://127.0.0.1:17654/`，candidate66后台正常退出，未关闭游戏或模拟器。当前API为`Idle`、run_id=0、quiescent=true，没有循环请求。
- EXE SHA256：`476ea69d81644b258304be77ea73c254e0ed115980515983c931c8cef707f8be`；服务实例：`7E48120A-08D5-411B-8205-6702DA099FE3`，PID 2424。
- 构建来源为`df3d22b`及未提交工作树，冻结差异SHA256为`70bfae6db99e9efc25bc58bc16b1c94ddbe4543521b1a506ca8c5016dc191a5d`。本报告部署回执是在打包之后补充，不冒充冻结构建输入的一部分。
- 正式profile部署前后SHA256均为`8C13E4593FDC70AF1A309FF58E84B79A65DB70F5B4D2FD2BF9FE93DF5018E883`，策略、VPN设置、日志开关及旧记录未覆盖。未commit/push，未启动新循环。

## 保留边界

- 第2、19轮是否确实反复操作同一角色，旧尾窗无法恢复；本次修复明确源码风险并为下次自然运行补证据，不重写历史结论。
- 指令菜单退出是行动开始的证据，不证明怪物已死亡或悬赏已完成；后续战斗/任务终点仍按原业务观察判断。游戏动画可能跨过采样窗口，真实效果留待用户下次运行观察。
- `MEMORY_UNATTRIBUTED`保留。此轮补的是可定位的释放边界，不宣称找到了OpenCV泄漏或内存已经稳定。
- 王城固定等待、ADB元数据调用等更大范围性能候选本轮不混入修改。没有开启新循环。
