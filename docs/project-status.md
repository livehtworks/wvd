# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `src/script.py` 中的 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死到本机路径。
- 图像识别基准为 900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，可使用既有 `mod` 扩展。没有项目数据库。
- 文本日志与诊断截图是运行证据，不能作为源码或分发资源提交。

## 当前阶段

- 当前工作包为 `WVD_MaaFramework_M3_Fix_and_M4_Pack_20260914`，实施基线 `4d7c4fa`，旧业务基线 `6585f407`。整体 **FIXES_INCOMPLETE / M4_PARTIAL_IMPLEMENTATION**，完整任务验收 **0/58**；不是等用户授权，剩余独立工作继续执行，停在 M5 前。
- 用户授权每阶段本地 commit，不包含 push/PR、旧版打包、设备发现/查询/连接/输入、VPN/游戏操作或生产切换。保护基线为本工作根的450文件清单，不能据旧验收推导本轮设备许可；私有工作根见 `../next/docs/m3-fix-validation.md`。
- 新版统一底座是独立 `next/` C++20/CMake、固定 Maa 5.13.0/OpenCV4.12.0、Vue/TypeScript；M1只读服务不链接设备核心。旧 Python 仍是唯一生产入口，配置/mod/资产和发行目录不动。
- M0 限定交付已收口，不重跑285组资源测试；`RESOURCE_UNRESOLVED` 保留。M3固定成本窗口已执行，不追加窗口挑PASS；`PERFORMANCE_UNRESOLVED` 保留。真实NEXT/Pause、Tesseract等价、任意原生阻塞取消和Spark仍未放行。
- M3发现句柄差异与 `CLEANUP_PENDING` 所有权已达有限修正边界，保持阻断，不再重跑该组。它不阻断无设备发现的独立离线业务建设。

### 正在验证

- 资源入口审计复现原生Pipeline模板/OCR漏检成员变化；补原生开始事件、单次检查与首因保留后五方法通过，同构建M2 102方法通过。含提交后首次截图新增成员故障、跨包同名、延后OCR和九项lease负例，见 `../next/docs/m3-integrity-entry-validation.md`；不等于成本/发现链放行。
- 自动移动地图依赖修正后六方法/十一场景通过（`197f531`）；业务确认帧龄检查五场景及M2 102方法通过（`33561a3`）。同一正式源码产物的陷阱专项五方法/七场景通过：完整33次输入、冷启动33次、中途恢复49次、连续两轮66次、提前回城及停止/拒绝；七场景已在重建前复核EXE身份。对应专题报告保留失败轮次，仍非当前新改动的完整回归。
- 默认对话首轮因帧龄失败，修正一五方法/二十二场景中仅连续页后置仍失败；十四选项、反证和停止/拒绝通过。修正二专用后置已提交待复验，见 `../next/docs/m4-default-dialogue-validation.md`。住宿待定状态断言通过，流程尚待验证。状态停止测试撞入原生等待，另分协作回调与受控截图阻塞；详情 `../next/docs/m4-stop-boundary-validation.md`，不放宽停止预算。
- 巨人专项已新增入本/两点/回城/间隔住宿有限图及状态回执，`fbc6196`构建完成，状态全组22方法通过，流程待验。停止两类边界亦已通过定点复验，详见专题报告，不代表任意原生等待可取消。最新生产保护清单复核450文件、零变化。

### 已有能力与剩余

| 范围 | 已有实现（专题的阶段产物证据不等于当前全量回归） | 剩余边界 |
| --- | --- | --- |
| M1 / 盘点 | 独立只读工作台、服务启停/版本查询、250函数/33配置/58任务/82入口及784分支固定索引 | 当前实现看M3/M4叠加表，不改历史索引伪称已迁移 |
| M2 / 核心 | 单一Run/Session所有者、输入门禁、当前帧身份、有限正常续段、取消/真正静止、根终态与子任务状态分离、原子结果 | 原生等待生产可中断性和长期资源未闭合；最新Gateway修正需再回归 |
| M3 / 资源视觉 | 默认/显式/排除ROI、boolean-only条件与位置动作、活动全文件封存、同调用Custom凭据、固定视觉和437张资源副本 | 资源入口修正复验、目录模板展开、剩余完整性矩阵与成本；真设备入口不放行 |
| M4.1–4.3 | 配置副本/来源/passthrough/CAS、图片mod持锁发布、58类型化任务、运行级状态/策略、43入本/路线/正常迭代图 | 全字段业务消费者、完整任务/扩展组合、剩余动态参数和状态交接 |
| M4.4–4.6 | 入城即停大地图输入、组队后真实住宿、角色恢复、地图/AutoMove冻结出口、头像/技能/等级/目标/Auto、多角色遭遇、完整有限开箱、Pause/死亡/复活、沙人/祝福/善恶提示、应用优先恢复与冷启动 | 默认/专项对话、未知页/冻结全链、住宿新保护验证、各任务恢复位置与副作用对账 |
| M4.7–4.8 | 陷阱专项七点链；巨人有限链待验；RunStore逐代次输入/业务摘要；善恶确认CAS写回新版profile | 巨人验证与其它十三专项实际链、收入/交付/补饵/跨周期住宿、真静止后的任务转交、58项成功/失败/停止/恢复及最终完整回归 |

- 全部任务仍保留分母与 `release_allowed=false`，不能把数据绑定、静态编译、子图通过算完整任务通过。逐项状态以 `../next/docs/migration/m4-task-status.json`、阶段汇总以 `../next/docs/m4-business-validation.md` 为准。
- 专项源码核对入口：`../next/docs/migration/m4-special-task-contracts.md`。基础目录十五quest之外，源码还有四个额外case，需登记扩展承接，不能伪造基础TaskID或删源码能力。
- 各阶段失败、定点修正、EXE/SDK身份和真实分母保留在 `next/docs/m4-*-validation.md`；旧报告不改写成新验收。根文档只维护此快照。
- 旧版维持上游2.8.7（`1c37b76`）同步和本地稳定性改动，见 `upstream-sync-2.8.7.md`；本轮不重新打包或更换其入口。

## 知识入口

- 执行注意项：`execution-notes.md`。
- 同步取舍和验收：`upstream-sync-2.8.7.md`。
- 历史稳定性改动与性能测量：`local-stability-and-performance-notes.md`（其中历史观察不替代本文件当前事实）。
- 独立 C++/MaaFramework 探针、脱敏结论与审核说明：`../validation/maafw-windows-20260913/`；不参与 WVD 构建、加载或运行。
- M0 最新边界与外部验证证据索引：`maafw-m0-review-resolution.md`；历史快照不替代本轮证据结论。
- M1 工程命令与架构：`../next/README.md`、`../next/docs/architecture.md`；完整映射与业务约束：`../next/docs/migration/README.md`。
- M2 运行文件与测试隔离的数据权威：`../next/docs/data-authority.md`。仅新增文件存储，没有数据库或正式新版配置入口。
- 当前 M3 修复与 M4 接续：`../next/docs/m3-fix-validation.md`、`../next/docs/m4-business-validation.md`、`../next/docs/m4-state-validation.md`、`../next/docs/m4-plan-validation.md`、`../next/docs/m4-workflow-validation.md`；资源所有权：`../next/docs/integrity-snapshot-contract.md`；字段和完整任务分母位于 `../next/docs/migration/m4-*`。
- 历史 M2/M3 报告：`../next/docs/m2-fix-validation.md`、`../next/docs/m3-device-vision-validation.md`，不覆盖当前阻断。
