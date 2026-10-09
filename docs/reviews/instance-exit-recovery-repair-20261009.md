# 模拟器退出后的循环恢复修复

用户要求修复第33轮停止并继续原100轮，前32轮已完成，续跑目标为剩余68轮。

## 实现

- 战斗施放、防御和公共选敌动作显式声明会话动作可在已证实实例退出后中断。默认输入仍不具有此许可；有持久副作用绑定的输入禁止声明该许可。
- NativeRunCoordinator保留失败Session的所有未确认输入回执。同一绑定实例已退出、通道静止、详情完整且全部挂起输入允许中断时，恢复实例/VPN/游戏，再由新Session从入口观察真实场景；记录recovery.input_interrupted，action_confirmed=false、input_replayed=false。
- 同Session读取恢复已完成实例重启时，携带真实连接代次交接证明；受保护战斗动作保留未确认状态，转交协调器重新规划。只有应用重启或断线时不扩大这项许可。
- MuMu崩溃瞬间可能保留Android启动标记。error900/901伴随进程已退出仍可读取身份，已退出时不读取在线端口；绑定核验先检查创建身份，退出分类为MUMU_INSTANCE_NOT_RUNNING，不把同一已退出实例误报为身份不符。

## 验证与运行

定向验证已通过：test_native_coordinator --instance-exit覆盖真实协调器、输入门禁、执行器及生命周期恢复链，包括外层恢复和同Session读取恢复两条路径：旧技能仅提交一次，实例仅重启一次，恢复后靠新画面到业务终点；历史回执仍在。未声明动作保持停止。test_native_flow --critical-recovery通过实例重启证明及单纯应用重启/未知送达保护；test_native_devices通过退出瞬间元数据边界。正式profile只读编译的巨人四种组合及共享蝎女图通过，巨人2295节点、繁中覆盖完整。这些定向验证隔离于正式数据，无设备输入。最终构建、部署及实机启动结果完成后补充于此。

本轮不人为制造模拟器退出。当前实例已退出，可以从正式任务入口验证实例重新拉起、VPN、游戏引导和后续任务推进；任意时刻再次崩溃后的长期恢复仍需自然运行证据。

## 部署与续跑记录

candidate132已完成最终产品构建、冻结打包及部署；PID45260，服务89989E6C-AA83-4889-ADDA-DFDA57662231，17654及原data。旧PID28972经管理器正常退出。正式profile SHA256为3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C，部署前后相同。

续跑请求71445cb5-74d8-4cad-b712-714703a95bcb，协调器EFA3C994-9B06-4E05-9F57-DA18C62D41CF，目标68轮；首次核对第1轮Running、repeat.active=true、completed_cycles=0，尚非完成记录。正式入口已重新启动实例2；EnsureVpn确认vpn_ready=true，StartApplication经过前台未到达的补发后确认application_running/foreground=true。启动回执在next/.local/giant132-68-20261009-71445cb5，运行日志在原data/runs/EFA3C994-9B06-4E05-9F57-DA18C62D41CF。

本轮未请求commit/push，源码及报告保留在工作树；用户.vscode未改动。

实机启动后，Boot_Attention、Boot_Title及Boot_DownloadZhHant均产生confirmed输入回执，资源下载/黑屏过渡结束后已进入Task_FirstDungeon_Battle_Actor战斗与技能等级选择。约155秒核对仍为Running、无error_code、目标68轮。该证据确认冷启动和任务推进，不将新批次第1轮运行中写成整轮完成。
