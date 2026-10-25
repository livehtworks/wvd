# 项目当前事实

## 接手入口

- 2026-10-21交接见 [工作交接](handoff-20261021.md)。先读本页；只有处理对应问题时才打开专项报告、日志或图片。
- 当前产品在 `next/`，C++20/CMake、Vue、OpenCV、ONNX Runtime。Maa及旧Python链不参与当前构建运行；历史源码和用户资产保留。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有批次，Coordinator拥有根运行及Session生命周期；没有旁路执行器或外部循环守护器。
- 正式 `profile.json`、`workflows/` 是配置及用户流程权威，见 [数据职责](data-authority.md)。[核心合同](../next/docs/core-contracts.md)及[执行注意项](execution-notes.md)按主题查阅。

## 当前操作边界

- 2026-10-25六个T0工作包的源码修复和离线验收完成；隔离分支`codex/t0-lifecycle-repair-20261025`，基线`01e5042`。66个原生入口及12个工具/前端阶段通过，见[修复验收](reviews/t0-lifecycle-repair-20261025.md)及`next/.local/t0-repair-261025/final-verification.json`。用户随后授权提交推送到个人远端、完整构建打包并部署正式服务，再从0开始新的200成功轮次；不计旧批次，监控保持暂停。发布和启动尚待本轮执行回执确认。
- 正式服务没有活跃循环：请求`b46c7a17-96d5-4121-b330-185896e03cf7`在file_copy阶段报`NATIVE_BUNDLE_LINK_FAILED:NTSTATUS=3221226085`，submission failed、repeat inactive/stopped；最后run94为Completed、三单元、result_saved/quiescent。`wvd`监控保持PAUSED。
- 本聊天恢复根`giant-200-resume-20261025-9dfea98c3b21429d8018e359838bb7da`已核run2–71共70份完整成功；run72为UserStopped/STOP_REQUESTED、仅1单元，排除。后续手动根`75f3dc44-1896-4e66-a3ce-ba69dc7bab99`已核run73–94共22份完整成功；两段各自原始目标为200，按根请求分开记录，不自行合并。界面新失败请求的completed_cycles=0不代表旧回执丢失。
- 正式旧版本的发布失败原因：3221226085即0xC0000265/STATUS_TOO_MANY_LINKS，共享检测模型实测1023链接。每包双别名先增至1024、下一条失败后回滚至1023，已由真实NTFS调用补证；不是设备连接错误。修复分支仅按系统实际容量错误从同一冻结租约复制回退，并补齐准入、连续所有权及持久阻断。正式服务尚未更新，因此旧入口尚未获得修复。
- 上一目标已取消，历史成功仍为40/200：3E95E158/run73–83共11份 + E65BF7A4/run1–29共29份；run30为UserStopped/STOP_REQUESTED、1业务单元，启动前核result_saved/quiescent且pending为空。旧40、剩160及更早100目标均不计入新200。
- 当前诊断及分段回执审计见`next/.local/bundle-link-check-261025-065401-1b573c/diagnosis.json`，含92份完整成功的路径/SHA、run72排除项、三个模型的链接数及最新失败请求。原启动证据仍在`next/.local/giant-200-resume-261025/`；其首次DEVICE_START_OBSERVATION_UNCONFIRMED已通过原API重连同一实例2解决，与当前硬链接问题分开。旧回执/pending及profile/workflows保持，不清理正式published目录。
- 暂停前最后巡检为06:34：已记录64次`navigation.input_no_progress`，已保存窗口确认至少12次程序主动StopApplication→StartApplication，全部重启总数因事件resync缺口未知；不按失败会话数推算，未归因MuMu崩溃。问题尚未修复，最后巡检证据见`monitor/check-20261021T223447Z/inspection.json`，首次故障原图保留于`monitor/check-20261021T203440Z/`（仅归档未看图）。
- 2026-10-22人工暂停的请求前状态、停止回执和静止确认见`next/.local/giant-200-new-261022/user-pause-20261021T230009Z/`；旧导航故障尚未修复，本次新授权仅恢复循环，监控继续暂停。

## 源码与部署

- 已部署旧候选的4组修复、测试素材和交接文档曾推送到个人远端 `fork/agent/local-stability-notes`，提交 `ee8dd8c53f1d37606753ed8d94c9529e40c3b43e`；`.vscode/`排除并保留。本轮T0源码修复在隔离分支，尚未提交推送。
- 当前使用 `next/.local/rescue-marker-candidate-261020/automationd.exe`，端口17654，正式data-root `D:/programcode/python/wvd/next/.local/c11-flow-product/data`；启动核验PID22208、start_filetime134370861777540010，后续操作仍须重核身份，不使用历史PID。
- EXE SHA256：`1b71deb345ea58aac2f4c6281b0dac066236c7c73afc3d1ecdfffd80bfff740d`。
- 正式旧候选product_inputs_sha256为`49479bba4f93c0bd84579615707a07d361cd4bc265ce4282643594e9430c8a3c`，1331项。本轮离线修复为`04361624…41c132`、1336项，完整身份见最终验证JSON；未生成发布BUILT回执或替换正式候选。
- 已部署修复：友方选人双排锚点/缺框四边与唯一布局；补给等待交回迟到宝箱奖励；同owner同规则Boot恢复栈复用且保留父账；救人圆形X原帧外观补充，仍由原party_death门禁及中心动作处理。
- 旧候选历史识别/app离线39入口、Boot修复runtime/app离线34入口通过，均不是游戏轮数。实机曾自然越过救人及再起并留下29份完整回执；不冒称全部故障根治。本轮66入口及12阶段是新的离线修复证据，未实机验收。
- 上次提交前核对过当时1331项输入、旧测试二进制、三组夹具15用例/20处原帧引用及打包成员；本轮最终验收另有完整源码/二进制桥接与正式配置终核，不混用旧候选身份。
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
