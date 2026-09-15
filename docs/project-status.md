# 项目当前事实

## 项目与职责

- 本项目是 `arnold2957/wvd` 的个人维护 fork，用 Python/Tkinter、OpenCV 和 ADB 自动操作英文版巫术 Daphne。
- `src/main.py` 管理任务线程和 GUI 生命周期，`src/gui.py` 管理配置面板，`src/script.py` 的 Factory 管理任务、战斗和恢复链，`src/utils.py` 管理资源与标准库日志。
- 截图通过 `ScreenshotBackendManager` 优先使用 MuMu IPC；既有授权的 ADB 后端用于 IPC 不可用时恢复。模拟器目录来自配置，不写死本机路径。
- 识别基准为英文界面、900x1600；保留模板缓存、同帧缓存、ROI、NEXT 多尺度遮罩与三角标记目标选择。
- 运行目录 `config.json` 是用户配置权威，任务数据来自 `resources/quest/quest.json`，保留既有 `mod` 扩展；没有数据库。
- 文本日志与诊断截图是运行证据，不是源码或分发资源；打包不得覆盖配置或清理日志、mod。

## 当前阶段

- 工作包为 `WVD_MaaFramework_M3_Fix_and_M4_Pack_20260914`，实施基线 `4d7c4fa`，旧业务基线 `6585f407`。
- 用户最新调整：不要求验证全部游戏任务。本轮新增公共能力已实现并完成必要定向回归，按调整后的范围收口；整体仍为 `M4_PARTIAL_IMPLEMENTATION`，不是完整M4或生产放行。
- 历史完整任务验收 `0/58` 与 `release_allowed=false` 保留，不以数据绑定、纯编译或子图成功改写。全量43/58任务矩阵不再作为本轮收口前置，也不据此暂停所有独立工程建设。
- 本轮授权阶段commit和推送个人 `fork` 的 `agent/local-stability-notes`；不含向原作者提PR、旧版打包、真实设备发现/查询/连接/输入、VPN/游戏操作或生产切换。停在M5前。
- 新版统一底座为独立 `next/` C++20/CMake、固定 Maa5.13.0/OpenCV4.12.0、Vue/TypeScript；M1服务仍只读，不链接设备核心。旧Python仍是唯一生产入口，旧源码/配置/mod/资产/发行目录不变。
- M0限定交付已收口，不重跑285组资源诊断；`RESOURCE_UNRESOLVED` 保留。M3固定成本窗口不追加挑PASS；`PERFORMANCE_UNRESOLVED` 保留。
- Metadata句柄差异与 `CLEANUP_PENDING` 所有权仍阻断设备发现链，已达原包有限修正边界。真实NEXT/Pause、Tesseract等价、任意原生阻塞取消、Spark未放行。

## 本轮结论

- `5419eff` 保存初始VPN单操作许可、冻结恢复配置、任务文字/上轮耗时摘要、PNG有界诊断及审核索引工具。两次构建错误分别由 `a58a585` 和 `007a7d6` 修正，失败日志保留。
- `007a7d6` 构建绑定16产物；M2 104方法通过，公共组70方法69通过1失败。诊断8方法全通过，11份执行身份、10份保存结果、83张PNG哈希/大小经独立核验。
- 同产物VPN/交接15方法13通过2失败：初始VPN6方法通过；交接9方法7通过2失败。7300秒注入时钟等待和保存后转交通过；停止等待/等待预算两例在进入等待前帧过期，保留BLOCKED，不宣称等待取消已经失败或通过。
- `7fa2957` 修复unsigned崩溃阈值越界漏拒，统一构建通过。最终M2 **104/104**（99.871秒）；状态39、诊断8、冻结恢复配置1方法合计 **48/48**（86.529秒）。批次执行前后16产物身份一致。
- 最终回归不覆盖全部任务，不取消既有帧龄阻断。旧记录缺逐次EXE/SDK身份的部分不补造；批次哈希不替代逐次证据。保护清单450文件无变化。
- 实现、构建身份、分项结果和剩余工作见 [阶段收口](../next/docs/m4-stage-closeout.md)；历史失败与修订保存在专题报告，不在滚动事实中保留过期的“正在运行”状态。

## 能力与边界

| 范围 | 当前承接 | 剩余边界 |
| --- | --- | --- |
| M1 / 盘点 | 独立只读工作台；250函数/33配置/58任务/82入口及784分支固定索引 | 盘点不等于业务等价或生产替换 |
| M2 / 核心 | 单一Run/Session所有者、当前帧输入门禁、有限续段、取消/真静止、根终态与子任务结果分离、原子结果 | 任意原生等待取消与长期资源未闭合 |
| M3 / 视觉资源 | ROI语义、boolean-only条件、模板/OCR契约、资源全成员封存、目录展开、Custom调用凭据 | 复杂图帧龄、成本/发现阻断与真实图片质量 |
| M4 / 配置状态 | 副本/CAS、mod持锁发布、冻结配置、策略消费/重置、遭遇/住宿/复活、任务摘要 | 界面和更新入口留在后续阶段，不从导入字段推导设备许可 |
| M4 / 业务流程 | 导航/移动/住宿补给、战斗/目标/Auto、宝箱、Pause/死亡、应用优先恢复、初始VPN、正常续段与任务转交 | 有实现和分项证据，不是全任务完整通过；真实生命周期端口未接 |
| M4 / 任务诊断 | 43普通任务图、15基础专项及4源码扩展、逐代次摘要/输入、善恶CAS、PNG配额/节流/原子存储、审核索引 | 已知复杂路线失败保留；常用任务组合按需补验，完整审核ZIP端到端未验 |

- 逐项任务事实为 `next/docs/migration/m4-task-status.json`，字段消费者与旧语义见 `m4-implementation-map.json`、`m4-semantic-audit.md`，不把历史盘点当当前实现状态。
- 陷阱、挖矿、住宿、蝎女/六手、钓鱼补给、吉尔、击退等已有各自分项证据；巨人/暗灯/半自动大恶魔、牛洞等复杂流程仍有保护门禁失败。具体失败、输入数与产物范围以专题报告为准。
- 旧版保持上游2.8.7（`1c37b76`）同步和本地稳定性改动；本轮未重新打包或更换入口。

## 后续工作

1. 优先定位共用识别帧过期：部分复杂图识别完成前已超过2秒门禁。检查重复识别、Context克隆和整图校验成本，不靠延长TTL或降阈值掩盖；原包修正次数已用完，另设明确诊断范围。
2. 真实运行前闭合宿主查询与清理所有权：Metadata/CLEANUP_PENDING仍阻断设备发现；资源增长归因、性能和原生阻塞取消仍未决。
3. 下一产品阶段M5承接界面与运行控制，随后M6新版发行、M7生产切换。未承接功能不得退役，不建立新旧生产并行入口。
4. 生产放行前做有限实机验证：启动/停止/恢复/VPN、真实NEXT/Pause、截图质量与长时稳定；执行前另确认设备权限，不用离线合成样本替代。
5. 按需补实际常用任务的失败分支、配置/mod组合；冷门任务全矩阵不作为当前前置。完整脱敏审核ZIP端到端、Spark仍未验。

## 知识入口

- 执行注意项：[execution-notes.md](execution-notes.md)。
- 旧版同步：[upstream-sync-2.8.7.md](upstream-sync-2.8.7.md)；历史改动：[local-stability-and-performance-notes.md](local-stability-and-performance-notes.md)。
- M0边界：[maafw-m0-review-resolution.md](maafw-m0-review-resolution.md)；独立探针：`validation/maafw-windows-20260913/`，不参与生产构建。
- 新版架构：[architecture.md](../next/docs/architecture.md)；命令：[next/README.md](../next/README.md)；文件数据权威：[data-authority.md](../next/docs/data-authority.md)。
- 当前验收：[m4-stage-closeout.md](../next/docs/m4-stage-closeout.md)、[m4-business-validation.md](../next/docs/m4-business-validation.md)、[m3-fix-validation.md](../next/docs/m3-fix-validation.md)。
- 专题证据：`next/docs/m4-*-validation.md`；迁移与消费者：`next/docs/migration/`。原始运行日志、SDK和截图仅保留在忽略的隔离目录。
