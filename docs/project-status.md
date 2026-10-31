# 项目当前事实

更新：2026-10-31。维护入口为[文档导航](README.md)和[剩余事项](maintenance.md)；本页只保存当前快照。

## 当前产品与部署

- 产品为 `next/` 的 C++20/CMake、Vue、OpenCV、ONNX Runtime；唯一执行链为 Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。详见[架构](../next/docs/architecture.md)。
- 正式候选 `next/dist/wvd-next-native/` 已部署执行包生命周期修复，源码提交 `3dd90b5`；EXE SHA256 `8203a84e45c921d5f20630f87f5875f85f787aa56d7c96f68e44feba4a9c0753`，产品输入 `214fbb20…04d1`/1357，619成员。本地构建、打包、候选预检与正式空闲启动通过，未进行游戏验收。
- 正式 data-root `D:/programcode/python/wvd/next/.local/c11-flow-product/data`，端口 17654。`profile.json`、`workflows/` 为配置及用户流程权威；[数据职责](data-authority.md)维护其解释。
- 2026-10-31正式服务实例 `74FB6855-B6FB-4301-B82D-87413693AEC5` 运行于原17654/data-root，已核Idle/run0/busy=false/quiescent=true/repeat=null及页面HTTP 200；设备未连接，原三实例关闭，监控保持PAUSED。旧历史转换已停止，本次未执行维护CLI或业务任务。
- 当前PDB GUID `92997ab5-efb2-4e86-a59a-520d07dca97c` / Age 8，已与新版EXE核对，保存在 `next/.local/deployment-recovery/execution-publication/current-symbols/automationd.pdb`。本次旧正式15670137候选由原打包事务保留，41份配置快照位于同一受保护恢复根；旧49ad/284e材料保持。此前旧历史恢复原件缺失的问题仍不能由本次部署补齐。

## 设备与批次

- MuMu 唯一安装 `F:/Applications/MuMuPlayer`，仍为 6.8.8.0。原 0/1/2 实例、名称和游戏数据保留；三盘首次开机前整盘 SHA 相同，三实例启动与新建默认 F 盘、原实例 2 游戏页及 WVD IPC 取图已验证。
- 正式 `EMU_PATH` 已经原 API 的 revision CAS 更新为 F 盘 Android 15 EXE；索引 2、ADB `127.0.0.1:16448` 和原 VPN 设置保持，其他 40 份配置 SHA 保持。详见[迁移记录](reviews/engineering/mumu-f-migration-20261030.md)。
- 当前独立目标根 `giant-200-20261030-a81556d0ff4449fea1fd1294c2779065`：**24/200，剩 176，暂停**。账本 `next/.local/giant-200-20261030/goal-ledger.json`；最后停止半轮不计成功。旧 40、70、22、39 等批次分别保留，不合并、不另开目标。
- 循环与监控不因文档维护恢复；共享 ADB 另有非本轮设备，不能随清理目录而停止它。

## 当前能力与未结问题

- 战斗 HUD/攻击选项消失的停滞恢复已修复并部署；选人网格、迟到奖励、同 owner Boot 恢复及救人标记沿原链。最近范围见[战斗修复](reviews/runtime/combat-animation-stall-20261030.md)，不声称 200 轮已完成。
- 发布资源共享及组合租约早已部署，新任务不依赖旧历史转换。正式旧发布历史已转换494份（含先行1份）；505份批次内493份提交、1份显式回滚、11份未执行，批次未续转。存储生命周期T0仍OPEN，不能将布局统一当作新增业务能力或事故关闭。
- 当前部署的任务/工作流/续轮统一使用一个派生执行槽位：同revision复用原文件，版本改变前核原始终态、未结副作用和活动租约，再退出旧槽位及资源；requests/runs历史保留原权威。隔离验收及空闲部署结果见[维护事项](maintenance.md)。本次没有转换、清理旧正式published目录，也不代表整个历史保留T0关闭。
- 原MuMu实例2冷启动及原VPN就绪后的游戏前台IPC取图已通过本轮有限诊断；历史 `libRenderer.dll` 与当前库SHA不同，不能用旧栈定位当前库。收尾内存377,114字节存活分配差额尚无归因，历史厂商崩溃与首次迁移冷连接失败仍保留原回执；已修复的旧导航缺口不重复列为现行故障。
- Unknown、未结付款/报告、低意志力和多人死亡保留原保护语义；不改设备版本、渲染、CPU/内存、驱动或 VPN。不关闭诊断及 240 张/128 MiB 生产最近截图策略。
- 线上 CI 已按用户决定退役；只做本地定向验收。`.vscode/` 和用户未提交修改继续保护。

## 工程收束

旧阶段和已版本化报告已合并为[两份主题摘要](history/engineering-summary.md)，不保留本地全量副本；当前报告按用途分类。重复离线夹具已核SHA移至D盘trash；剩余47份巡检材料也已核完，归并为10个必要证据文件，其余277文件同盘迁出，待分类数为0。正式原始结果、未决输入和当前测试素材保持。数量和恢复入口见[资料与证据管理](material-retention.md)。

个人master已承接最新战斗修复和两轮文档收束。旧工作区17处修改的49份原始版本、两patch及索引核验通过，Git管理元数据已封存，失效worktree登记已退出；整树忽略材料仍无恢复证明。本轮过度优先处理旧发布布局，耗时与工具当前功能收益不相称；将仍承担恢复职责的原件放入普通trash也不妥。用户清空trash后出现维护读失败，批次已停止；原生journal现为494 committed/2 rolled_back，无pending。74份保护文件SHA保持，实际空闲启动验证后服务已关闭。原位记录见[维护事项](maintenance.md)，不恢复业务循环或监控。

本次废弃的应急副本、配置副本、转换脚本和重复执行记录已核对后整体移至`D:/trash/wvd-discard-maintenance-20261031`：1,853文件/424,062,741逻辑字节，原formal-maintenance目录已不存在。必要事实留在[原T0报告](reviews/incidents/t0-storage-lifecycle-20261026.md)和既有台账，不依赖该垃圾目录。最新问题仍为恢复区设计、整体回滚保证、C09保留/退出合同及未归因运行观察；不继续旧历史批量转换。
