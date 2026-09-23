# 额外流程事件：本地验收记录

日期：2026-09-23。基线：`e6f46b5e164bdd71e80f42257b0c2502a989fee4`。最终状态：同一分支的未提交工作树；未生成新版交付目录，未 commit/push。

## 结论

`PARTIAL_NOT_DELIVERABLE`。作者事件的契约、编辑、发布期候选、Maa 子任务、输入回执和受控续接已接入，并通过若干固定 Maa 离线场景；但 A03 的原生导航遇怪、A07 的真实业务计数、A11 的 `task_stage` 原生 Dispatch 所有权和 A15 的正式 `Application` 装配运行没有达到本包规定的证据门槛。不得将此工作树替换现有 `next/dist/wvd-next`，也不得宣称所有游戏任务已支持事件。

本轮不操作 MuMu、ADB、VPN 或游戏，真实设备操作计数为 **0**；真实 NEXT、Pause、维护识别均未验证。

## 实际代码

| 项 | 已实现 | 边界 |
| --- | --- | --- |
| E01 组合编辑 | slot `_CallN` 继承有效 `repeat_limit`；编辑快照可撤销新增可选字段 | 定向结构和六轮 Maa 场景通过 |
| E02 规则与闭包 | 作者 JSON/schema、继承/禁用、节点覆盖、公共 handler 引用、传递闭包、语义素材预检 | `Application` 正式装配未离线执行 |
| E03 回执 | `InputGate` 事件作用域挂起父回执、子输入、返回后新帧确认/replan | 不把已发输入伪装成未发送 |
| E04 分派 | 单个 `FlowEvents` 分派器；发布期 Maa 候选和 Custom 检查点；子任务状态核验 | 原生业务图的作用域覆盖未充分验证 |
| E05 时间/停止 | 父观察暂停、嵌套去重、总墙钟上限、停止和 `external_blocked` 终态 | 嵌套暂停的时长精度尚无独立计时断言；原生永久阻塞不可中断边界保留 |
| E06 WVD 兼容 | 战斗、开箱和诊断入口新增事件检查点；未重写旧分派 | **功能缺口**：作者规则尚未绑定到原生子图节点，这些检查点在原生子图内无有效规则；`task_stage` 所有权与业务计数也未完成 Maa 验收 |
| E07 资源/UI | 事件语义目录、HTTP 保存往返、事件面板、运行事件路径显示 | 网络和维护真实配方缺失；UI 没有证明实际装配运行值 |
| E08 集成 | 固定 Maa 离线案例、Windows EXE 与 Vue 构建、隔离 HTTP/UI 检查 | 未满足全部 A01-A16；没有晋升发行候选 |

正式定义由 `WorkflowRepository` 保存。编辑入口为 `WorkflowPage.vue` 的事件面板，可新增规则、选择检测条件和处理流程、指定返回方式、撤销/重做并保存。运行入口仍为 `Application::prepare_workflow -> assemble_workflow -> PublicFlowLibrary::compile -> with_boot_recovery -> publish_workflow -> RunCoordinator`；`workflow_session.cpp` 将事件作用域写入发布包，`ExecutionSession` 建立分派器。HTTP 编辑检查成功不等于上述整条运行链通过。

## 限定验收

状态中的“通过”只指列明的离线断言，不扩展到真实游戏。

| 组 | 结果 | 证据或缺口 |
| --- | --- | --- |
| A01 | 部分通过 | 六轮 Maa 输入通过；浏览器撤销/重做与保存通过；尚无独立可选字段浏览器断言 |
| A02 | 部分通过 | `test_author_workflow.exe` 结构检查通过；HTTP 定义往返通过；正式连接前全部依赖闭包未实跑 |
| A03 | 未通过验收 | 合成作者目标经 Maa handler 续接通过；未验证原生导航 NEXT/遇怪仍回同一任务点 |
| A04 | 通过（合成） | 父输入后 overlay，子输入 1 次，父不重发；新帧完成，总底层输入 2 次 |
| A05 | 通过（合成） | 候选和输入之间插入 overlay；先处理事件，父输入重核 |
| A06 | 部分通过 | 合成背景与 overlay 同时存在可优先处理；真实 Pause/网络资源未验证 |
| A07 | 未通过验收 | 合成 chest→combat→network 三层 Maa 子流程通过；真实开箱与战斗统计/策略未验 |
| A08 | 部分通过 | 父已发输入后不重发；领取/支付的业务回执未验证 |
| A09 | 部分通过 | handler 失败时父输入为 0；真实 Error、缺图及拒绝输入的组合未覆盖 |
| A10 | 部分通过 | 合成维护为 Interrupted/external_blocked、0 输入；真实维护识别与普通未知分界未验 |
| A11 | 未通过验收 | 原生子图节点没有事件作用域；未证明 `task_stage` 原生 Dispatch 与通用 handler 不重复处理 |
| A12 | 部分通过 | handler 中停止得到 UserStopped，父不再输入；父等待与多层嵌套停止未覆盖 |
| A13 | 部分通过 | 400 ms 总墙钟截断 2500 ms 等待；代码修正嵌套重复暂停；缺独立父观察精度断言 |
| A14 | 通过（合成） | 2100 ms 截图后输入成功；输入后连接代次变化拒绝旧确认且不重放 |
| A15 | 未通过验收 | 隔离 HTTP/UI 保存重开成功；测试直接调用公共编译/发布，未从 `Application` 装配入口启动 |
| A16 | 部分通过 | 无事件 Maa 对照 1 次输入完成，未跑历史全矩阵；隔离端口和进程清理通过；无最终交付候选 |

固定 Maa 证据：`next/.local/m4-workflow-0dlbzulw` 中四个最近回归场景共 8.263 秒，4/4 通过；`author-event-receipt/run/*/1/events.json` 的 `flow_event.enter` 为序号 51、`flow_event.exit` 为序号 109，事件 `network`、来源 `Author_act`，退出为 `reobserve`。同场景 `output.json` 显示 `Completed`、`backend_calls=2`、`mismatch=false`。这条轨迹证明合成作者父动作、handler、返回确认，不证明原生任务点或策略统计。其他定向案例保存在相应 `next/.local/m4-workflow-*` 与 `next/.local/event-work-pack-e6f46b5-20260923/maa-*.log`，包括输入前事件、replan、六轮 slot、无事件对照、慢截图和连接换代。

| 能力 | 代码 | 资源/语言 | 结构 | 固定 Maa 离线 | 真实游戏 |
| --- | --- | --- | --- | --- | --- |
| 遇怪战斗 | 作者 handler 与战斗检查点 | `combat_active` 来源存在，未在本轮实机复核 | 有 | 合成 handler；原生链未验 | NOT_RUN |
| 宝箱转战斗 | 原生开箱检查点与作者嵌套规则 | `chestFlag` 标记 UNVERIFIED | 有 | 合成三层；计数未验 | NOT_RUN |
| 网络提示 | 事件规则可配置 | 提示/重试动作缺真实配方 | 有 | 合成 retry | NOT_RUN |
| Pause | 事件规则可配置 | 探针标记 UNVERIFIED | 有 | 无真实 Pause 案例 | NOT_RUN |
| 维护 | `external_blocked` 终态 | 缺真实维护配方 | 有 | 合成维护、0 输入 | NOT_RUN |

## 构建与现场

- 本机模型实际 ID：未提供。固定 Maa 为仓库现有 5.13.0；SDK 未升级。测试设备和画面是离线替身，编译/发布、Maa 调度及 InputGate 为真实代码。
- Windows 构建 `next/build/Release/automationd.exe` 成功，SHA256 `BBFA7E1B3246698B6A9D223B0F10134A037939CB2630C4CF98303D4B3D14E85C`。Vue `next/web/dist/assets/index-CpKgy5IA.js`、`index-DVTSlBRj.css` 构建成功。结构检查输出 `W03_AUTHOR_WORKFLOW_PASS`；`git diff --check` 通过。构建日志在 `next/.local/event-work-pack-e6f46b5-20260923/build-final.log`。
- 浏览器最初点击新增规则无响应，定位到 Vue reactive proxy 传给 `structuredClone` 引发 `DataCloneError`；改为纯 JSON 规则拷贝后，浏览器新增、撤销/重做及保存成功。隔离 HTTP `127.0.0.1:17863` 上的编辑往返通过。
- 总墙钟场景最初被测试夹具无效失败边和孤立节点挡在编译期，修正夹具后第一次执行通过。另一个旧版 `inn-tracked` 定向例因隔离夹具缺 `image/inn_icon.png` 在编译前失败，不计作事件回归通过或失败；未运行旧任务全矩阵。
- 本轮隔离 HTTP 服务已停止（Ctrl-C，退出码 1），端口 17863 监听数 0，匹配本轮 data-root 的 `automationd.exe` 进程数 0。未关闭或改动用户原有进程。
- `src/`、`config.json`、`mod`、用户日志、旧 `dist/wvd`、现有 `next/dist/wvd-next`、未跟踪 `.vscode/` 均未改动。资源增长 `RESOURCE_UNRESOLVED` 和任意 SDK 原生永久阻塞的取消边界未在本轮销项。

继续交付前须先界定原生 Dispatch 的单一所有者，并把合法的事件作用域传到实际原生子图节点；再补齐 A03/A07/A11/A15 的有限离线链和 A13 精度断言。不能通过简单地在所有子节点铺规则来制造双处理。真实资源仍按独立任务采样；不再用大矩阵或重复长跑代替这些证据。后续任何实机验证须由用户明确选择范围。
