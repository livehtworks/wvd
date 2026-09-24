# WVD 业务层

`tasks/` 生成任务与作者定义的有限步骤；`native_program.cpp` 在发布期转为 `workflow/FlowProgram`，运行时只有 `runtime/FlowExecutor` 推进。`native_operations.cpp` 将业务操作注册为可观察、可停止的短步骤；`state.cpp` 只保存策略、计数和未确认副作用，不拥有设备或图像。

`vision/native_recognizers.cpp` 和 `native_asset_resolver.cpp` 提供 WVD 专用的定位与素材语义，调用中立 `recognition/Service`。`combat/`、`chest/`、`navigation/`、`supply/`、`recovery/` 分别负责本领域规则；跨领域事件由执行器单一调用栈处理。初次启动通过生命周期计划检查 VPN/游戏状态；普通 NoHit 不产生模拟器重启权限。

目前直接类型化任务工厂及完整实机任务链仍未验收，见 `docs/project-status.md`；不要把编译成功或离线作者流程成功写成蝎女任务完成。
