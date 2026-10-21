# 项目当前事实

## 接手入口

- 2026-10-21交接见 [工作交接](handoff-20261021.md)。先读本页；只有处理对应问题时才打开专项报告、日志或图片。
- 当前产品在 `next/`，C++20/CMake、Vue、OpenCV、ONNX Runtime。Maa及旧Python链不参与当前构建运行；历史源码和用户资产保留。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有批次，Coordinator拥有根运行及Session生命周期；没有旁路执行器或外部循环守护器。
- 正式 `profile.json`、`workflows/` 是配置及用户流程权威，见 [数据职责](data-authority.md)。[核心合同](../next/docs/core-contracts.md)及[执行注意项](execution-notes.md)按主题查阅。

## 当前操作边界

- 用户在2026-10-21接手时明确：最新循环已经取消，不要重新开始。`wvd`监控交接时核实为PAUSED，保持暂停；不自动恢复监控、启动循环、部署或操作游戏。下述剩160仅为历史目标差额，不是待执行任务。
- 停监控当时没有停止游戏循环或服务。2026-10-21 22:37只读交接查询17654连接被拒绝；这不证明游戏当前画面、进程退出原因或停止发起者，本轮没有重启。
- 磁盘最后已知段：E65BF7A4-E24A-46D5-BB1F-A3798B4188F9，根 `giant-189-rescue-marker-20261020-3e575bc2b41747bc80ae86662f520f7d`。
- 同一200目标保存成功为 **40/200，剩160**：原3E95E158/run73–83共11份 + E65BF7A4/run1–29共29份，交接逐份核Completed、3业务单元、result_saved、quiescent及基数SHA。E65BF7A4/run30为UserStopped/STOP_REQUESTED、1业务单元，排除；不能自动恢复半轮。
- 证据：`next/.local/handoff-261021-2237/receipt-audit.json`。原续段账本/部署确认在 `next/.local/rescue-marker-resume-261020/`；`resume.ps1`已执行，禁止重跑。上述为磁盘核对，不是当前运行验收；历史100目标及其它失败/维护半轮均不加入。

## 源码与部署

- 本轮提交基线为 `8cdb4645d6daf83a3f71c9b8da087a5357b82e49`；用户已授权将后续4组修复、测试素材及交接文档提交并推送到个人远端 `fork` 的 `agent/local-stability-notes` 分支，范围见交接；实际提交及远端状态以Git为准。用户独立 `.vscode/`排除并保留。
- 最近已知部署：`next/.local/rescue-marker-candidate-261020/automationd.exe`，端口17654，正式data-root `D:/programcode/python/wvd/next/.local/c11-flow-product/data`。历史PID50592不是后续操作依据；每次重核实例、创建时间、EXE及根请求。
- EXE SHA256：`1b71deb345ea58aac2f4c6281b0dac066236c7c73afc3d1ecdfffd80bfff740d`。
- product_inputs_sha256：`49479bba4f93c0bd84579615707a07d361cd4bc265ce4282643594e9430c8a3c`，1331项。这是原候选身份，不宣称交接后的整个工作区已重新构建。
- 已部署修复：友方选人双排锚点/缺框四边与唯一布局；补给等待交回迟到宝箱奖励；同owner同规则Boot恢复栈复用且保留父账；救人圆形X原帧外观补充，仍由原party_death门禁及中心动作处理。
- 最近识别/app离线39入口通过；此前Boot修复runtime/app离线34入口通过，二者不是游戏轮数，也不表示重跑全部历史矩阵。实机曾自然越过救人及再起，随后有29份完整轮次回执；不冒称救人必然成功或全部故障根治。
- 提交前只读复核：当前1331项产品输入哈希与上述39入口回执一致，运行时测试二进制与34入口回执一致；三组夹具共15用例、20处原帧引用及准备帧哈希有效，新增打包成员与源文件、登记哈希和大小一致。复用既有离线证据，未重跑测试、部署或启动循环/监控。
- 原build.py、package_functional.py、manage_service.ps1构建/打包/预检/部署分别有证据；发布时27份正式权威文件哈希保持。交接没有重新构建、测试或部署。

## 保持的能力与未解问题

- 既有路线迟到交接、EntryReady原目标续接、同unit入本/任务点回执、宝箱/奖励/再起/哈肯职责、归还凭回执先记账后关闭、住宿已付款不重复均保持；不得清业务pending或让下一unit借用旧回执。
- 单人救人保留已消费策略，只有真实再起确认才重置；多人死亡、低意志力、Unknown保护停止不得擅自抵消。保留现场不妨碍无副作用的隔离诊断，不得以重复静态巡检代替修复。
- MuMu内部libRenderer.dll崩溃原因及RESOURCE_UNRESOLVED仍未归因；已有dump不等于根因已解，0xc0000409不直接解释为栈溢出/OOM/取图致因。
- 不改驱动/模拟器版本/渲染/CPU内存/VPN，不上传日志转储，不恢复full dumpsys window；只使用可见MuMu实例2，禁止宝石购买、抽卡和卖装备。
- 保留debug/performance/recognition/memory及240张/128MiB最近截图策略；减少的是代理读取/展示，不关闭生产取证能力。验证必须独立可丢弃data-root，不能在正式目录写测试数据。

## 历史入口

- [交接前完整状态快照](reviews/project-status-before-handoff-20261021.md)：保留原文，包含大量过期身份和授权，只作历史证据，不作为当前操作许可。
- 专项报告及精确验证入口统一见[工作交接](handoff-20261021.md)，无需每轮全文通读。
