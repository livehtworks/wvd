# WVD 业务层

`tasks/` 生成任务与作者定义的有限步骤；`native_program.cpp` 在发布期转为 `workflow/FlowProgram`，运行时只有 `runtime/FlowExecutor` 推进。`native_operations.cpp` 将业务操作注册为可观察、可停止的短步骤；`state.cpp` 只保存策略、计数和未确认副作用，不拥有设备或图像。

`vision/native_recognizers.cpp` 和 `native_asset_resolver.cpp` 提供 WVD 专用的定位与素材语义，调用中立 `recognition/Service`。`combat/`、`chest/`、`navigation/`、`supply/`、`recovery/` 分别负责本领域规则；跨领域事件由执行器单一调用栈处理。初次启动通过生命周期计划检查 VPN/游戏状态；普通 NoHit 不产生模拟器重启权限。

`tasks/run_builder` 负责原生任务选择、轮数、交接和恢复纯决策；`tasks/locale_assets` 仅在旧原生图片名兼容边界替换配方，不能改写作者显式图或调用者覆盖。只读 `worldmapflag` 可解析为繁中的联合地图证据，点击目标不能由此生成。`tasks/bounty_visit` 的 Reveal 编译公共公会开页定义；英文 Report 保留原有提交链，繁中缺可信提交配方时完整任务在准备期拒绝。`authoring/workflow_validator` 校验中立文档，WVD binding 仍在本层编译。城市、公会页和宝箱阶段的公共视觉配方由语义目录生成于构建期；其他动态 boot/地点配方的发布期冻结仍待补全。

目前直接类型化任务工厂及完整实机任务链仍未验收，见 `docs/project-status.md`；不要把编译成功或离线作者流程成功写成蝎女任务完成。
