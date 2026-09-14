# M4 原生输入时序

## 原因与修改

普通阻塞阶段的自动退场回归曾在底层输入前失败：动作新帧到场景确认约 1.80 秒，原生 Click 又等待约 200 毫秒，最终触发 2 秒 STALE_ACTION_INTENT。该次后端输入为零，保护正确，但时序存在隐式成本。原始失败留在 `m4-workflow-j6ocfs9g/auto-return-prompt`，不覆盖。

固定 [Maa 5.13.0 Context 源码](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.13.0/source/MaaFramework/Task/Context.cpp) 的 RunActionDirect 只封装动作参数，没有节点级时序；传入 action.param.pre_delay 不能解决问题。

`Context::native_action` 改为同一 Context 的 RunAction：私有临时节点显式指定前/后延迟、冻结等待均为零。SDK 仍创建子动作克隆上下文、执行原生 Click/Swipe、返回真实 ActionDetail，最终 Controller 门禁及动作后新帧确认不变。没有控制器直点、改全局默认、扩张帧 TTL、跳过等待或另建执行器；业务图显式延迟仍由原节点持有。

这只去除非业务声明的默认等待，不证明所有复合识别都能在 2 秒内完成；识别过慢仍应被拒绝，后续不能靠延长有效期解决。

## 证据

- 最终构建 `m4-native-timing-final-build.log` 通过。首轮因缺少 runtime_files.hpp 声明失败，补齐直接依赖后重建，失败日志保留。
- 两个新增真实 SDK 测试在独立 Bundle 设置前/后各 3000ms 默认延迟，Click/Swipe 均正向确认完成，合计 1.994 秒；同一 2 秒门禁仍有效。日志 `m4-native-timing-tests.log`，证据 `m2-runs/runtime-6ehip67h`。
- 原自动退场失败用例定点补验通过，9.947 秒，证据 `m4-workflow-87imy_ml`；未修改识别阈值或增加重试次数。
- 完整 M2 回归 102 项通过（原 100 项加 2 项时序测试），100.125 秒，`m4-native-timing-m2-regression.log`。M3 定点 ROI/Custom/组合条件 3 方法通过，28.066 秒，`m4-native-timing-m3-regression.log`。保护基线 450 文件无变化。
- 保留 M3 发现/所有权、RESOURCE_UNRESOLVED、PERFORMANCE_UNRESOLVED 和实机未验证边界；不扩大这次定点时序修正的结论。

仅离线 FakeDevice + 固定真实 Maa SDK；无旧生产文件/配置写入，无实际游戏操作。
