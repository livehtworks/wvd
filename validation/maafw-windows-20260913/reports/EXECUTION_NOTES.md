# 执行注意项

- 所有操作限定到独立验证根目录；不要在生产 WVD 工作目录导入 main/gui/utils 或运行生产任务。
- Windows 中文路径使用进程级 UTF-8 manifest；不要求用户修改系统全局区域设置。
- 构建使用现有 VS2022/MSVC 与 CMake，SDK 固定为 v5.13.0。未授权升级框架。
- 准备脚本必须明确提供 -RepoRoot；设备路径/实例信息留在 private/target.json，不作为跨电脑常量。
- 系统/Clash 横屏 1600×900，WVD 竖屏 900×1600。分场景校验，不修改设备显示配置。
- MuMu 的 is_android_started 不等于 ADB 即刻 ready；有界轮询 boot property，禁止以 kill-server 作为恢复捷径。
- 普通 ADB 设备列表命令在服务不存在时会自动启动本地 daemon；本轮没有执行 kill-server。
- 常规游戏资源更新已得到用户授权；付费、登录、清除数据和额外系统权限不在授权内。
- 所有自定义 ABI 回调捕获异常。禁止同一 Tasker 在自己的回调里 post_task().wait()。
- Context 及 clone 的使用留在执行线程；clone 由框架持有，不存在公开独立 destroy 接口。
- 停止先保存业务原因，再提交框架停止；框架状态表被清空后可能返回 Invalid，不能倒推业务成功。
- 清理顺序为任务停止且退出 → Tasker → Controller → Resource → 回调状态。不要在任务活动时清缓存。
- 默认启动脚本只运行离线探针，不自动控制游戏。失败返回非零退出码。
- 原始失败日志必须保留。每次正式运行应生成唯一输出目录；两个 Unicode 成功补测曾复用 a2 输出目录，命令 stdout/stderr 仍按 UUID 保留，不能把它们当作两份独立完整事件轨迹。
- P4-LIFETIME 未达增长斜率门槛，不允许用改变预热、阈值或反复挑选成功结果替代定位。

