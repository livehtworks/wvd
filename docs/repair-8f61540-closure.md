# 8f61540 修复与分层工作包收口（2026-09-25）

实施基线为 `8f61540eafa61413852c2c3a85cb81c090e2161f`；最终源码提交、构建时间、EXE/前端/资源哈希见候选的 `next/dist/wvd-next-native/DELIVERY_STATUS.json`。旧 Python 程序、`config.json`、`mod`、用户运行记录及 `.vscode` 未改。旧 Maa/M0-M4 报告不作为本轮验收。OpenCV 分配异常的专门复核见 [内存报告](opencv-memory-review-20260925.md)。

| 项 | 实现状态 | 构建/静态证据 | 产品/现场结果 | 剩余问题 |
| --- | --- | --- | --- | --- |
| R01 语言 | 已改：`resource_locale.hpp`、`Application::start_task` 在连接前冻结并预检；空语言保留原图，非法值拒绝 | C++ Release 与 Vue 构建通过 | 本轮未启动设备 | 非英文原生全任务覆盖未证 |
| R02 图编辑 | 已改：`graphMutations.ts` 原子处理增删换组/顺序，一次变更对应一次撤销快照 | Vue 类型及构建通过 | 隔离正式候选的流程说明撤销/重做/保存重开通过（桌面 1 项） | 复杂分支增删换组的界面交互未证 |
| R03 业务失败 | 已改：`FlowProgram`/`FlowExecutor` 区分子块普通失败返回与致命故障，父调用走失败边 | 原生编译及一个作者编译定向检查通过 | 未遇自然业务失败 | 真实子块业务失败链未证 |
| R04 运行事实 | 已改：执行器标量栈、事件/未决输入，Coordinator/RunStore/工作台同源显示；回执记录尝试、门禁与底层提交 | 原生、Vue 编译通过 | 本轮未发游戏输入 | 事件、取消及输入未知结果的自然现场待证 |
| R05 资源 | 部分：旧原生映射保留显式覆盖；城市、公会页及宝箱阶段配方从语义源生成；诊断非 OCR 走 WVD 绑定；打包同步两个作者 JSON，并拒绝 EXE/资源语义源哈希错配 | 构建、资源清单哈希及生成探针等价检查 | 未检查本轮公会/地图实际框 | 其余内部 boot/地点动态依赖未全部发布期冻结；分类和用途仍需逐项核对，不能标全面闭合 |
| R06 公会 | 部分：Reveal 调用同一 `PublicFlowLibrary` 定义；已到列表零输入；错误的“领取首条悬赏”入口阻断 | 作者编译定向检查通过 | 旧候选有公会链证据，本轮新候选未跑 | Report 繁中提交缺可靠素材，明确不可执行；未证明正式工作台按钮与原生同版本入页 |
| R07 内置同步 | 已改：`.builtin` 来源元数据、差异状态、显式 CAS 同步与旧文档备份；不启动时覆盖 | 原生及 Vue 编译通过；现有用户定义只读核对 | 未对用户数据执行同步 | 固定版本引用需用户显式处理；当前用户修改过的条目仍保持冲突 |
| R08 分层 | 部分：`run_builder` 接任务/恢复决策，`authoring/workflow_validator` 接无设备校验，图关系移到前端 feature | C++/Vue 编译通过 | 无实机改变 | `native_recognizers` 中其余动态配方/跨帧职责未全部拆分，不为行数而扩改 |
| R09 分配异常 | 已补有界诊断，异常仍是 Error | 精确复核既有宝箱 Run 和约 34 MB 异常；无资源长跑 | 本轮未自然重现 | `RESOURCE_UNRESOLVED`，根因未确认 |
| R10 交付 | 候选元数据区分基线、HEAD+diff、构建、EXE/前端/资源和实机范围；当前事实文档已更新 | Release、Vue 构建、候选打包校验及隔离启动；修复暂存目录 EXE 占用导致的 Windows 替换失败 | 编辑页定向 1 项通过；游戏 `NOT_RUN_THIS_BUILD` | 不能继承旧候选游戏 PASS |

构建命令：`cmake --build next/build --config Release --target automationd test_native_author -j 4`、`npm run build`（`next/web`）、`next/build/native/Release/test_native_author.exe`。另用隔离数据目录在 `31927` 启动正式候选，仅运行 `native-workbench.spec.ts` 的桌面“流程编辑撤销”一项，通过后按 PID 结束自有服务。没有执行 `validate.py`、全量测试矩阵或模拟器长跑。候选入口为 `next/dist/wvd-next-native/启动WVD原生版.bat`。如需验收，用户先选定有限游戏操作范围；不自行领奖、交付、购买、复活、跳轮或刷怪。未出现的维护、特殊网络、箱转战斗和真实 NEXT/Pause 分支保持 `NOT_OBSERVED`。

最后现场：本轮没有连接/停止 MuMu、启动游戏或 Clash，也没有启动常驻候选服务；没有本工具新增监听端口或未决输入。用户已有模拟器状态保留。源码/构建完成不等于完整工作包的实机验收完成。
