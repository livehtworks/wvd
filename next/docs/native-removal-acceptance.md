# Maa 移除工作包执行记录

> 历史验收快照：下文 `DEVICE_READONLY_ACCEPTED=NOT_RUN` 等标记只描述 Maa 移除当轮，不是 2026-09-25 的当前产品状态。后续原生候选已有有限公会菜单实机证据，但本轮 `8f61540` 修复候选尚未实机运行；以 `../../docs/project-status.md` 与 `../../docs/repair-8f61540-closure.md` 为当前事实。

基线：`376ca780fa7432afb48031b048d340531ae8d5c7`。本记录描述独立候选的当前证据，不代表旧 Python 程序已切换生产。执行范围遵守工作包的无实机输入约束；提交/推送由用户于本轮单独授权。

## 交付状态

| 标记 | 结论 | 证据边界 |
| --- | --- | --- |
| `SOURCE_MAA_REMOVED` | PASS | 工作包自带只读审计扫描 286 个活动文件，命中 0；旧集成、旧阶段测试及旧 MaaDeps 锁只在 `next/archive/` 保存，不参与当前构建。 |
| `BUILD_MAA_FREE` | PASS | 隔离 Release 构建成功；候选 4 个 EXE/DLL 的 `dumpbin /imports` 无 Maa；实际 `automationd.exe` 模块列表只有候选目录的 OpenCV/ORT，无 Maa。 |
| `PRODUCT_OFFLINE_ACCEPTED` | PARTIAL | 正式 Application 作者流程与蝎女任务入口、配置与流程编辑、停止、结果保存、候选启动已过；下表列出的业务和完整 UI 验收未完成，不能标 PASS。 |
| `DEVICE_READONLY_ACCEPTED` | NOT_RUN | 没有连接当前 MuMu 或使用真实帧。 |
| `GAME_SCOPE_ACCEPTED` | NOT_RUN | 没有点击、领取、住宿、跳轮、讨伐或运行真实任务。 |

## 当前架构与资产

`Vue → HTTP/Application → WorkflowRepository + ProfileStore → FlowProgram → NativeRunCoordinator → FlowExecutor → RecognitionService / WVD Operations / InputGate → DeviceSession`。运行时只有一个节点推进器和一个输入所有者。MuMu IPC 仅由自有只读 CaptureHost 加载，ADB 用于指定设备命令，scrcpy 只承接控制通道；OpenCV 和 ORT 直接由候选目录加载。运行中作者定义和资源冻结，原历史包只读。

旧 `src/`、`config.json`、`mod`、日志、`dist/wvd`、`next/dist/wvd-next`、`.vscode` 没有被本次改写。已有作者数据未用于离线检查；本轮使用临时数据目录。悬赏只刷新/提交、不接取，王城塔楼身份、旅店/郊外文字及哈肯两级返回事实均保留。迁移责任见 `native-migration-map.json`。

## 验证记录

- 锁版依赖 SHA256 校验、`python next/tools/build.py`、`python next/tools/validate.py` 均通过。另在 `%TEMP%/wvd-native-clean-20260924` 重新配置并构建 Release 服务和 CaptureHost；MSVC 仅报告临时目录增量构建警告 `MSB8029`。
- 当前 `dependencies.lock.json` 只保留使用中的 Boost、JSON 和工具链条目；旧 `opencv.lock.json` 完整归档为 `next/archive/legacy-locks/opencv.lock.json`。原生依赖唯一活动锁为 `native-dependencies.lock.json`。旧数据/依赖说明归档，当前权威文档已更新。
- 原生定向检查覆盖执行器事件范围与嵌套退出、公共 slot 六轮/两处调用、ADB/scrcpy 报文、CaptureHost 人为挂死后的回收与代次、停止、直接 OpenCV 与真实英文 ORT、正式 Application 作者运行和原生任务入口。引用中的公共流程通过正式 API 删除时被拒绝。
- 最终独立候选在端口 `31919` 启动；Playwright 桌面/390px 手机各验证工作台配置保存重开、流程页面、说明撤销/重做/保存重开，共 4 项通过。退出后 `31919` 无监听、该候选进程已退出。未连接模拟器。
- 候选 `automationd.exe` SHA256 `FF10BAF47390C994E4FB58E45852C5C8C71FE619DA7F5D6C3190AB6C41BBBF31`；`wvd-capture-host.exe` 为 `C66D519BB4FA4578D1ECB60438C9E90A006951AA4D844409280AE91A8A8B54B4`。`opencv_world4120.dll` 为 `0455560C75BC456B5F758C94AF653B6F0397ED8A28B9605CF52C62E7C8F18781`；`onnxruntime.dll` 为 `7788F3F38E9A339003F7D7E1BF47F928287CF409BB5273273149E1282FBF503F`。

## R00–R12 实施状态

`PASS` 仅指该条工作包要求有直接证据；`PARTIAL` 保留具体剩余工作，不用源代码存在代替行为验收。

| 项 | 状态 | 未完成部分与下一次验收条件 |
| --- | --- | --- |
| R00 保全与影响盘点 | PARTIAL | 基线、保护边界、旧适配器去向及悬赏/王城/旅店/郊外/哈肯事实已记录；未对全部旧 API、动作、配置字段和素材动态路径形成逐项语义映射。 |
| R01 独立依赖与构建 | PASS | 活动锁、CMake、候选依赖均不从 Maa SDK/MaaDeps 获取；独立 Release 构建和哈希核对通过。 |
| R02 中立定义与编译 | PARTIAL | 原生工厂仍先组装项目中立的编译期 JSON 图，再降为强类型 `FlowProgram`；须直接构造强类型步骤/子定义，并复核旧字段映射、参数/slot/返回/快照身份。 |
| R03 唯一执行器 | PARTIAL | 唯一 `FlowExecutor` 已接正式入口；顺序、调用、事件局部样例通过。完整原生任务的错误路由、调用返回、根终点与未确认副作用组合尚未闭环。 |
| R04 事件与回执 | PARTIAL | 作用域规则、覆盖层、嵌套退出和父输入保留有定向证据；完整网络/战斗/开箱嵌套、非幂等输入、失败/取消展开和计时并集仍缺产品证据。 |
| R05 直接视觉 | PARTIAL | OpenCV 已直接接入并有模板正例/错误分离；旧特殊识别算法与同一真实素材的 ROI、变体、框和阈值等价尚未逐项核对。 |
| R06 独立 OCR | PARTIAL | 锁定英文模型在真实 ORT 中执行；缺字典、坏模型、负例、推理期间取消及繁中场景不具备完整验收。 |
| R07 设备通道 | PARTIAL | ADB/scrcpy 控制报文及自有 CaptureHost 挂死回收有离线证据；真实 MuMu 身份/帧、控制握手、ADB 重连和持触点取消均未验。 |
| R08 线程/停止/终态 | PARTIAL | 协调器停止、等待取消、结果落盘及候选关闭已测；连接/OCR/滑动各阶段停止、清理失败与跨进程历史恢复未全覆盖。 |
| R09 WVD 全任务接线 | PARTIAL | 任务入口已指向原生装配，蝎女在离线正式入口到达 `Task_`；其余任务业务副作用、策略/计数、交接续段及最新繁中资源仍未在同一新链证实。 |
| R10 工作台/作者/历史 | PARTIAL | 配置、流程编辑保存重开、撤销重做与隔离作者运行通过；全页面功能、公共定义返回、预览、历史与运行中的来源定位尚未完整验收。 |
| R11 限定机制回归 | PARTIAL | 已用有限原生定向检查而非旧矩阵；N02–N18 的部分断言仍未形成正式入口证据，见下表。 |
| R12 完全去依赖与交付 | PARTIAL | 活动树审计、PE 导入、实际模块、候选启动/退出通过；`PRODUCT_OFFLINE_ACCEPTED` 未达 PASS，故候选不能发布为完成或替换生产。 |

## N01–N18 验收缺口

| 项 | 状态 | 已有证据 / 缺少的决定性证据 |
| --- | --- | --- |
| N01 clean build/启动 | PASS | 无 Maa 的独立 Release 构建、候选启动与活动树审计通过。 |
| N02 公共调用/slot | PARTIAL | 六轮与两处独立调用编译检查通过；参数隔离、实际执行计数和 source_path 未在正式 Application 同链核对。 |
| N03 编辑/删除/撤销 | PARTIAL | 正式 API 拒绝删除被引用流程，浏览器撤销/重做/保存重开通过；展开返回、可选字段撤销删除及未知字段往返未同链验。 |
| N04 输入前覆盖层 | PARTIAL | 执行器覆盖层优先有局部检查；背景目标同时命中、handler 返回后重新判断父场景的完整输入轨迹未验。 |
| N05 4253ms 后置 | NOT_VERIFIED | 没有专门证明父输入恰一次、4253ms 后置仍有效及独立业务确认的正式链轨迹。 |
| N06 行走遇怪 | NOT_VERIFIED | 没有同一任务点目标/战斗计数的一次完整走怪返回轨迹。 |
| N07 箱/战斗/网络嵌套 | PARTIAL | 事件嵌套退出有定向检查；三类真实策略叠加、单 dispatch 与计时并集未全证。 |
| N08 非幂等未知结果 | PARTIAL | 回执与未确认输入阻断已编码；领取/报告网络中断后绝不重发的产品轨迹未验。 |
| N09 四类异常 | PARTIAL | 识别 Error 与 NoHit 分离；维护、加载黑帧、真正未知、坏模型四者的独立结果/零重启未成组验。 |
| N10 handler 退出 | PARTIAL | 退出动画预算有局部检查；失败、取消、自递归可见时的栈与回执展开未成组验。 |
| N11 双重计时 | PARTIAL | 暂停区间实现及嵌套出口有检查；父有效预算、总期限、重入阶段的可控时钟断言不完整。 |
| N12 五阶段 Stop | PARTIAL | 等待/协调器与假采集停止已查；连接、真实取帧、推理中 OCR 和滑动的停止/清理全链未证。 |
| N13 CaptureHost 挂死 | PASS（离线边界） | 自有假 helper 挂死后仅它被回收，两次采集代次分离，主检查进程仍可运行；不外推真实 MuMu DLL。 |
| N14 ADB/scrcpy 协议 | PARTIAL | 直接报文编码与 ADB 状态解析通过；完整版本握手、触点/按键序列和全部坏消息拒绝未证。 |
| N15 OpenCV/ORT | PARTIAL | 模板 Hit/Error、真实英文 OCR 正例通过；同素材 ROI/框/负例、坏字典/模型和繁中能力未证。 |
| N16 正式双任务装配 | PARTIAL | 作者流程完成并落盘；蝎女原生任务到达 `Task_` 后停止。原生子调用 scope、策略和计数各一次未证。 |
| N17 原有界面/数据 | PARTIAL | 工作台配置和流程编辑已验；策略重命名引用、探索反向 SKIP、预览、所有面板及任务按钮真实效果未全验。 |
| N18 打包/关闭 | PARTIAL | 候选启动、4 个 PE 导入、实际模块及本地端口进程关闭通过；完整递归 delay-import/设备通道退出与结果边界未全证。 |

OCR 三个文件的 Git blob SHA 与 [MaaCommonAssets 固定提交](https://github.com/MaaXYZ/MaaCommonAssets/tree/dabcd4681ac990dc4361de26416d986abd80e4aa/OCR/ppocr_v3/en_us) 完全一致，且该提交根目录 [MIT 许可证](https://github.com/MaaXYZ/MaaCommonAssets/blob/dabcd4681ac990dc4361de26416d986abd80e4aa/LICENSE) 已随源码/候选保存。此为来源和协议记录，不替代法律意见。

候选可用于**离线查看和继续开发**，不能称为该工作包整体验收完成，更不能对当前游戏开启无人值守。实机只读和游戏范围按本包均为 `NOT_RUN`，不是漏跑；需由后续明确选定范围。旧 Maa 路径的资源归因属历史问题，新构建通过不等于替它完成归因。已退役的 M0–M4 工作包和验证工具是历史证据，不作为另一套同时生效的待办；仍影响产品的事项已归并到 R09/N16/N17。
