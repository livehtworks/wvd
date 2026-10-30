# 项目当前事实

更新：2026-10-31。维护入口为[文档导航](README.md)和[剩余事项](maintenance.md)；本页只保存当前快照。

## 当前产品与部署

- 产品为 `next/` 的 C++20/CMake、Vue、OpenCV、ONNX Runtime；唯一执行链为 Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。详见[架构](../next/docs/architecture.md)。
- 正式候选 `next/dist/wvd-next-native/`；EXE SHA256 `15670137f3229039d088363af5580cf72665857b3a0a0af767f955c96811fa75`，产品输入 `fc734057…3eb72c`/1354，619 成员。文档整理不重建、不部署。
- 正式 data-root `D:/programcode/python/wvd/next/.local/c11-flow-product/data`，端口 17654。`profile.json`、`workflows/` 为配置及用户流程权威；[数据职责](data-authority.md)维护其解释。
- 2026-10-31 迁移验收结束时正式/隔离 WVD 服务和原三实例均已关闭，监控 `PAUSED`。本轮只读检查未见 automationd，MuMu 管理器已打开；没有操作或关闭它，不凭历史 PID 控制进程。
- 匹配 PDB GUID `92997ab5-efb2-4e86-a59a-520d07dca97c` / Age 4，位于 `next/.local/giant-200-20261030/stall-fix/release2/automationd.pdb`。旧 49ad/Age 2 及 284e 回滚材料保留；正式历史未转换。

## 设备与批次

- MuMu 唯一安装 `F:/Applications/MuMuPlayer`，仍为 6.8.8.0。原 0/1/2 实例、名称和游戏数据保留；三盘首次开机前整盘 SHA 相同，三实例启动与新建默认 F 盘、原实例 2 游戏页及 WVD IPC 取图已验证。
- 正式 `EMU_PATH` 已经原 API 的 revision CAS 更新为 F 盘 Android 15 EXE；索引 2、ADB `127.0.0.1:16448` 和原 VPN 设置保持，其他 40 份配置 SHA 保持。详见[迁移记录](reviews/engineering/mumu-f-migration-20261030.md)。
- 当前独立目标根 `giant-200-20261030-a81556d0ff4449fea1fd1294c2779065`：**24/200，剩 176，暂停**。账本 `next/.local/giant-200-20261030/goal-ledger.json`；最后停止半轮不计成功。旧 40、70、22、39 等批次分别保留，不合并、不另开目标。
- 循环与监控不因文档维护恢复；共享 ADB 另有非本轮设备，不能随清理目录而停止它。

## 当前能力与未结问题

- 战斗 HUD/攻击选项消失的停滞恢复已修复并部署；选人网格、迟到奖励、同 owner Boot 恢复及救人标记沿原链。最近范围见[战斗修复](reviews/runtime/combat-animation-stall-20261030.md)，不声称 200 轮已完成。
- 发布资源共享及组合租约已在当前代码/候选中承接；**正式旧发布历史转换仍为零，存储生命周期 T0 继续 OPEN**。工程收束、同盘 trash 搬移和部署不能代替事故关闭。
- MuMu `libRenderer.dll` 崩溃原因、部分资源/内存归因及旧导航问题仍不能宣称全部解决。首次迁移冷连接失败未能归因，原回执保留。
- Unknown、未结付款/报告、低意志力和多人死亡保留原保护语义；不改设备版本、渲染、CPU/内存、驱动或 VPN。不关闭诊断及 240 张/128 MiB 生产最近截图策略。
- 线上 CI 已按用户决定退役；只做本地定向验收。`.vscode/` 和用户未提交修改继续保护。

## 本轮资料收束

旧阶段和已版本化报告已合并为[两份主题摘要](history/engineering-summary.md)，不保留本地全量副本；当前报告按用途分类。重复离线夹具已核SHA移至D盘trash；剩余47份巡检材料也已核完，归并为10个必要证据文件，其余277文件同盘迁出，待分类数为0。正式原始结果、未决输入和当前测试素材保持。数量和恢复入口见[资料与证据管理](material-retention.md)。
