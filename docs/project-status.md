# 项目当前事实

## 当前产品

- 旧版 Python/Tkinter 程序仍保留，未覆盖 `src/`、旧 `config.json`、`mod`、日志或 `dist/wvd`。
- 新版 Windows 可操作程序位于 `next/`，技术底座为 C++20、MaaFramework 5.13.0、OpenCV 4.12.0、Boost.Beast 与 Vue 3。
- 新版唯一服务入口为 `next/native/app/main.cpp`，装配层为 `next/native/app/application.*`；设备、运行、配置和流程数据均由后端持有，Vue 不自行模拟状态。
- 可双击交付目录为 `next/dist/wvd-next`，入口为 `启动WVD新版.bat`。新版用户数据保存在 `%LOCALAPPDATA%\WvdNext`。

## 已接通能力

> **阻断状态（2026-09-22）：** WVD Next 的真实启动链尚不可用。真实环境未能完成“VPN -> 启动游戏 -> 处理启动页 -> 进入任务”，且恢复升级曾导致模拟器被反复重启。不得将下列组件能力解读为可交付运行能力；详见 `next/docs/real-device-startup-incident-20260922.md`。

- 工作台可编辑并保存模拟器、目标、探索、战斗、战斗方案、任务点、日常和高级设置；战斗方案通过左侧可搜索列表切换，右侧只编辑当前方案；旧配置只读导入新版副本。
- 明确选定 MuMu 路径、ADB 地址和实例编号后，可从界面连接、断开、截图并显示异步失败原因；截图与任务执行共用设备所有权。
- 可直接运行现有任务，启动定义读取所选任务的有效配置；生命周期恢复继续承接游戏启动、重连和已启用的 Clash/VPN。
- 流程编辑器支持新建、复制、保存、删除、任务转编辑副本、节点参数、连接语义、撤销/重做、选中节点调试、已保存版本运行和停止。
- 作者流程已接入识别、受控点击/滑动/返回键、可中断等待、有限重复、失败出口、战斗/开箱/业务确认和现有任务阶段。
- 试识别支持当前帧和 900x1600 本地 PNG；模板、OCR 与 WVD 既有识别模式返回 `Hit / NoHit / Error`，不触发设备输入。
- 运行结果显示真实 Run 状态、当前编辑节点、耗时、输入统计、失败原因和有界诊断截图。

## 本轮实操事实

- 打包版 `0.5.0-local` 已在 `http://127.0.0.1:17652` 启动。
- 通过成品界面连接用户明确选择的 MuMu 实例并取得 `MUMU_EXTRAS` 实际截图；检查时前台为安卓启动器，画面为 1600x900。
- 通过成品界面启动并停止“安全停止示例”，真实结果为 `run_id=1 / UserStopped / USER_STOP`，输入尝试、执行和拒绝均为 0。
- 已生成 `[宝箱]狼洞2f（可编辑副本）`，其准备、入本、路线/战斗/开箱节点可继续编排；未替用户启动真实游戏任务。
- 本地 900x1600 图片模板试识别得到 `Hit`，WVD Pause 模式得到有效 `NoHit`，证明两条试识别入口均接入真实识别器。

## 保留边界

- 本轮不以全任务矩阵作为交付前置；具体游戏任务效果仍由用户从完成的界面实操后按实际问题修复。
- `RESOURCE_UNRESOLVED`、少数复杂识别的成本和任意第三方原生阻塞的取消边界仍保留，不据此声称长期挂机稳定性已经证明。
- 新版是独立可操作版本，尚未替换旧版默认生产入口，也没有新增安装器、自动更新或跨平台实现。

## 知识入口

- 使用与构建：[next/README.md](../next/README.md)
- 本轮交付：[windows-functional-delivery.md](../next/docs/windows-functional-delivery.md)
- 架构：[architecture.md](../next/docs/architecture.md)
- 数据权威：[data-authority.md](../next/docs/data-authority.md)
- 执行注意项：[execution-notes.md](execution-notes.md)
