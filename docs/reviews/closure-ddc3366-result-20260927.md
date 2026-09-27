# 稳定性与工作台收束结果

工作包：`WVD-ddc3366-CLOSURE-20260927`。实施跨日至2026-09-28。本报告只证明指定离线范围，不证明游戏通过。

## 基线与现场保护

- 仓库：`D:\programcode\python\wvd`；分支：`agent/local-stability-notes`；HEAD：`ddc33660e7e740b747b5d3699fd94fa0e25681f0`。
- 开工时受影响跟踪文件无用户修改；无关未跟踪`.vscode/`保留。未执行commit、push、pull、reset、clean或全局配置修改。
- 本轮证据根，下文简称`E`：`D:\programcode\python\wvd\next\.local\cl-ddc-927`。构建、故障夹具、API数据和浏览器证据均在本轮独立目录。
- 原证据根较长，资源缓存嵌套哈希、UUID和素材名后触发Windows路径长度限制；停止相关自有进程后整体移到上述短路径。失败日志保留，没有删除资源或绕过哈希校验。
- 不调用17654现场服务，不连接设备、不取真实截图、不操作游戏、不关闭游戏/模拟器/VPN，不部署。历史42/100和剩余58轮仅引用此前报告，不续跑、不重新核算。
- `src/`、运行配置、mod、旧日志、旧dist、旧候选和锁定依赖未修改。正式作者资源源文件未修改。

## K01–K08：修改、断言与证据

### K01：元数据故障分类

结果：限定范围完成。

- 新增`next/native/devices/metadata_read_fault.hpp`，由`device_session.cpp`的实例查询调用。仅把明确完成清理的`METADATA_TIMEOUT`转成既有`ObservationUnavailable`；缺静止、释放句柄、helper退出或pending I/O归零证明时不重试。
- 保留实际预算、耗时、错误与有界原始报告。分类器不发命令、不睡眠、不调度；恢复仍由现有Session/FlowExecutor负责，未增加第二套重试链或放宽实例身份校验。
- META-01/02在`test_native_devices.cpp`验证成功、清理缺失、取消和非法报告；READ-01/02/03在`test_flow_executor.cpp`验证原栈恢复、同一窗口耗尽、恢复中停止，输入始终一次且pending原依据不变。
- 证据：`E/logs/metadata.log`、`recovery.log`。三条READ结果分别为恢复成功、`OBSERVATION_RECOVERY_EXHAUSTED:METADATA_TIMEOUT`、`CANCELLED`。测试缩短自己的恢复窗口以验证时序，生产60秒窗口未改。

### K02：异步写入锁

结果：限定范围完成。

- `useWorkbench.ts`、`useWorkflowEditor.ts`在首个await前获取写锁并冻结请求内容；迟到回调检查生命周期/代次；失败保留草稿、未保存标记与方案改名信息。
- 工作台和流程页的字段、导航、画布、节点/连线修改、撤销重做、复制、导入、提取、同步及删除统一受锁约束。`App.vue`禁止保存中切走；全局停止不在编辑锁内。
- EDIT-01–06覆盖延迟响应、双击只有一请求、冲突不覆盖草稿、相同内容重试、附加写入入口和停止独立。作者页补充真实鼠标拖动、Delete/撤销快捷键、直接派发按钮事件，随后核对重试提交内容完全一致。连线与节点回调的锁同时做源码检查。
- 证据：`closure.spec.ts`中的工作台写入、作者写入、附加写入锁三个用例；`E/logs/playwright-5.log`及最终作者专项日志`playwright-12.log`。受控响应只隔离后端延迟，不替换Vue页面和store逻辑。

### K03：任务选择与持久化基线

结果：限定范围完成。

- 有效任务草稿与最后实际保存的配置分开。任务只读切换不会冒充保存；仅最后一次请求可更新目标/参数/错误/loading，失败恢复一致的选择。
- 字典/数组战斗策略、点位策略等在建立比较基线前规范化。取消放弃、恢复保存版本和灵庙快捷切换走同一链；读取revision变化明确报冲突，不静默混装配置。
- TASK-01–06覆盖乱序成功/失败、仅换任务dirty、撤销/取消、保存冲突、灵庙失败不改日期、旧形态与隐藏值保持。正式API另验证隐藏任务覆盖在方案改名后同步引用，并在服务重开后读取一致。
- 证据：`closure.spec.ts`任务选择/选择拒绝/CAS/功能保留用例；`closure-native.spec.ts`；`E/logs/playwright-5.log`、`playwright-11.log`与对应JSON附件。

### K04：唯一运行镜像和停止入口

结果：限定范围完成。

- 新增App级`useRunSession.ts`、`RunSessionBar.vue`、`RunDiagnostics.vue`。工作台和流程页共享同一运行镜像，不再各建轮询器或启动身份。
- 1500ms轮询，run/device独立且分别最多一个GET在途；读取失败或超过5秒新鲜度阈值显示未知，保留最后事实、禁止新开始、保留停止。新鲜度随轮询节拍评估，不承诺恰好5000ms刷新UI。
- 停止始终调用current端点，准备期保留request_id。启动HTTP结果未知时保留意图和幂等身份；命令使旧GET失效，命令后新观察完成前不出现空闲窗口。前端不计算业务完成，不调度下一轮。
- RUN-01–09覆盖跨页、准备期、失联、Failed但尚未静止、device GET单独失败、未知启动结果、停止与旧GET竞争、保存中停止及诊断预览可关闭。
- 后端`storage_error`、`secondary_errors`、未决输入/恢复问题在常驻摘要可见；详细执行栈、事件、命令和截图仍在折叠诊断中。
- 证据：`E/logs/playwright-5.log`会话/准备与陈旧/布局用例；实际API测试没有启动任务，不能据此宣称真实停止或恢复已通过。

### K05：工作台紧凑分组

结果：限定范围完成。

- 工作台改为常用参数、战斗方案、设备与高级三个分组。首屏保留目标、语言、循环方式、开始/保存/撤销；App常驻停止。战斗方案仍为左侧列表选中编辑，不铺满所有方案。
- 数值字段104px、短选择控件200px并受窄屏约束；大屏利用多列，小屏降列。无连接时不显示大型空截图；已有截图可放大，Esc关闭且焦点返回，不挡住停止。
- UI-01–07对应1440×900、1366×768、1920×1080、800×900、390×844五种视口，三个分组和流程页共20张截图；断言首屏关键动作、文档无横向溢出、草稿保持、短字段宽度、焦点和停止可达。
- 实际查看过1440桌面常用页和390窄屏战斗页截图。其余视口有布局断言与截图，不将自动截图数量冒充逐张人工审美验收。
- 证据：`E/playwright-attempt5/`布局用例目录；`playwright-results.json-attempt5`；`logs/web-build-5.log`。

### K06：迁移盘点退出产品

结果：限定范围完成，正式素材链保留。

- 删除迁移专属`MigrationPage.vue`、`useInventory.ts`、`InventoryTable.vue`、`ItemDetail.vue`以及API/样式中的专属内容。主导航只保留工作台、流程编辑。
- `next/tools/build.py`不再调用inventory。`tools/inventory/generate.py`保留为手动审计工具，默认输出改为`.local/audit/migration-baseline/`，不写web公共资源；本轮未运行生成器。
- `next/docs/migration/`原11份文档/JSON移至`next/docs/archive/migration-baseline-ddc3366/`，逐一哈希相同；新增ARCHIVE说明历史身份，不把冻结报告当当前事实。
- 专属2份公共报告、437个旧预览退出web公共目录，副本保留在本轮忽略证据目录`retired-public-migration`、`retired-reference-assets`。新候选web仅index与构建JS/CSS，不含这些目录。
- MIG-01–04对应导航/请求拒绝、构建调用检查、候选内容、归档哈希。MIG-05通过正式catalog、语义资源列表、作者流程保存、上传图片识别接口及打包素材校验核对。上传探针用正式Inn模板合成900×1600本地图片并精确命中[60,450]起点，不取设备帧，也不证明真实场景识别率。
- 证据：`E/migration-original-hashes.json`、`migration-closure.json`、`logs/stage-3.log`、`logs/playwright-11.log`，`playwright-results.json-attempt11`中的`formal-upload-probe`附件。

### K07：空间准入和写入失败闭环

结果：限定范围完成。

- `Application::require_storage_space()`在新请求及task/workflow准备前检查available至少1GiB，已有request_id的幂等回执先于检查；低空间为`RUN_STORAGE_SPACE_LOW`，查询异常/available未知为`RUN_STORAGE_SPACE_QUERY_FAILED`。
- Application仍是唯一续轮所有者；下一轮空间不足保留已完成计数。Coordinator把计时/诊断不完整写入secondary_errors；结果写入失败或详情不完整不允许继续新轮/交接。
- SPACE-01–05使用真实Application队列、Coordinator、RunStore与仅测试目标内的替身：低空间无prepare/connect/capture；阈值准入；第1轮完成后第2轮被拒且计数仍为1；计时和终态写入故障不继续输入、不伪造完整结果；已有哨兵文件哈希不变。
- 证据：`E/logs/application-4.log`、`E/app-1FF89F40/{space,timing,terminal}-result.json`。空间查询注入仅测试friend使用，不增加automationd测试模式。
- 对现场数据目录只做过一次文件大小读取：action-timing 40文件/24,250,434字节；近期帧240文件/26,025,493字节；runs其它446文件/156,951,673字节；其余90,467文件/10,679,478,578字节。runs三项合计207,227,600字节，总计10,886,706,178字节。其余包含资源缓存，不能全部称为运行日志；文件数不等于完成轮数，因此不推算每轮平均。未清理任何历史。

### K08：实际执行器时序与隔离API

结果：指定离线范围完成。

- `test_native_author.cpp --closure-transitions`装配正式工厂/编译器/FlowExecutor，以受控叶子观察驱动跳轮残帧、黑屏/网络、剧情返城、迟到哈肯、自动战斗结束、住宿提交/未付款、公会退出、郊外/哈肯。不是只比较图节点字符串。
- TRANS-01–09含两个反证和两个启动场景，共11份轨迹；输入依据、pending和业务事实写入`E/TRANS-*.json`。TRANS-05正确结果是战斗已结束时安全失败`combat.auto_not_confirmed_before_battle_end`，不是伪造自动攻击确认。
- 时序测试找出并修复`business_condition.hpp`未开放`/inn_payment/submitted`读取的问题：住宿已提交与未付款反证现分别成立，不新增付款策略或清空账目。
- 正式候选以18755、新建`api-data-*`目录启动，无`--offline`参数，无设备或运行POST。UI受控响应在18754；未登记请求拒绝。正式API验证保存、合法旧revision冲突、隐藏任务改名引用、公共调用/插槽/交接定义保持、重开后的真实落盘值。
- 新增测试夹具`next/web/tests/closure-server.mjs`只管理本轮端口、自有子进程、新数据及重开；没有编入automationd或产品页面。Playwright retries=0、workers=1，仅两个新测试文件，不跑旧矩阵。

## 功能保留与退出清单

- 保留任务类别/目标、专用覆盖/清除、语言、循环次数/一直循环、灵庙切换、恢复/休息参数、反向布尔设置、宝箱角色、高级次数、模拟器路径/编号/ADB/Clash和设备操作入口。
- 保留方案列表、新增/删除/改名、角色技能等级/目标/频次、完成语义/重置时机、点位绑定、骷髅与头像两种特殊战斗条件及隐藏已保存头像值。控件保存重开在受控UI及正式profile接口分别验证，不调用实际设备操作。
- 保留流程画布/布局/节点/边、条件/事件/成功失败交接、公共调用/插槽、导入/复制/提取/同步、撤销重做、资源选择、识别探针、开始和单节点调试入口。运行相关接口本轮只验证前端请求和原生替身，不执行实机业务。
- 仅退役上述迁移展示链；既有`workbench.spec.ts`只替换两项迁移专属断言，没有删除整个文件。`native-workbench.spec.ts`设备设置定位改到对应tab；`test_inventory.py`读取冻结归档，不强迫当前产品继续生成旧盘点。
- 除上文生产文件，修改`next/native/CMakeLists.txt`将作者时序测试链接既有runtime；扩充四个现有原生测试文件；新增`playwright.closure.config.ts`、`closure.spec.ts`、`closure-native.spec.ts`；同步`next/README.md`、`next/docs/architecture.md`、项目状态及执行注意项。
- 新增生产文件仅工作包允许的四个：元数据分类头、App会话store、常驻运行条、诊断组件。完整路径清单见`E/final-source-manifest.json`。

## 实际构建、命令与退出码

命令从仓库根执行，`E`为上述绝对目录；原生命令在`next/build/native/Release`，依赖使用既有锁定DLL。最终生产代码没有在验证后追加功能修改，只有一行尾空白清理。

| 命令/范围 | 最终结果与证据 |
|---|---|
| `cmake --build next/build --config Release --target automationd test_native_devices test_native_flow test_native_author test_native_application`（按受影响目标分次构建） | automationd/devices/flow在首轮构建完成；作者测试修正variant调用/链接后build-3成功；application夹具修正后application-build-6成功。首轮整体退出非0，不能写成一次全成功 |
| `test_native_devices.exe --closure-metadata` | 0，metadata.log |
| `test_native_flow.exe --closure-recovery` | 0，recovery.log |
| `test_native_author.exe --transition-contracts` | 0，transition-contracts.log |
| `test_native_author.exe --closure-transitions`（设置WVD_CLOSURE_ROOT） | 0，transitions-2.log |
| `test_native_application.exe --closure-application E/candidate/pack E/candidate/data/quest.json` | 0，application-4.log |
| `npm.cmd run build`（next/web） | 0，web-build-5.log，Vue类型检查/Vite构建 |
| Python导入`next/tools/package_functional.py`并调用`stage(本轮全新候选目录)` | 0，stage-3.log；没有调用会启动实机验证的package流程 |
| `npx.cmd playwright test --config=playwright.closure.config.ts` | 第5次修订验证9项UI全通过，正式API用例当时失败；保留该次退出1和全部截图 |
| `npx.cmd playwright test --config=playwright.closure.config.ts closure-native.spec.ts` | 最终0，playwright-11.log；正式保存/重开/上传识别通过 |
| `npx.cmd playwright test --config=playwright.closure.config.ts closure.spec.ts --grep 'UI-作者写入'` | 0，作者锁补充专项，playwright-12.log |
| `git diff --check` | 0，logs/diff-check-final.log；LF/CRLF提示不通过修改全局Git配置消除 |

没有执行ctest、旧pytest/全任务矩阵、validate.py、资源长测或游戏循环。失败后修改对应实现/夹具才重跑指定项；没有自动重试挑成功，失败输出和trace都保留。

## 候选路径及实际身份

候选：`D:\programcode\python\wvd\next\.local\cl-ddc-927\candidate`。

| 项目 | 实际值 |
|---|---|
| 源基线 | `ddc33660e7e740b747b5d3699fd94fa0e25681f0`，工作树有本轮未提交改动 |
| automationd.exe SHA256 | `084cde9d2bfb1f7259900c835326d89c73f115832a7a3433885592b5fdc0214f` |
| web/index.html SHA256 | `7351547193f34321a956e5f069745c842fbdd0b8810c01812230dfe398007346` |
| pack revision | `098de476cc18101ea15795625870033b49d26bb0012554bd74fcd7cb485c97c4` |
| resource catalog SHA256 | `49399a4ec3802c586154f23e3a70350099bb38c3d2710958c741682de7335a77` |
| stage时间UTC | `2026-09-27T16:13:33.944606+00:00` |
| 交付状态 | `BUILT_NOT_GAME_ACCEPTED` / `NOT_RUN_THIS_BUILD` |

`DELIVERY_STATUS.json`中的stage时工作树diff哈希是`db062efec08d6eba681b929354633aad0fba274fa3bfe092fbbefe304e091c2e`，不是报告完成后的整个工作树哈希。

现有stage会先同步作者源到`next/packs/wvd`。基线本来就存在作者源与包副本不一致的下载后置条件；本轮没有更改作者源，候选沿现有stage使用作者源并校验素材/固定依赖。stage造成的两份工作树包副本改动已精确恢复到开工内容，未提交额外业务资源变更。候选资源revision包含既有作者同步和OCR模型，不能把revision差异归因于删除了正式图片。正式`next/resources`、`next/packs`在最终git diff中无变更。

## 未验证项、失败项和剩余风险

- 本轮不证明真实MuMu断线/退出恢复、真实NEXT/Pause/宝石保护全部语言布局、所有游戏路线、真实转场成功率或长期内存稳定性。均为NOT_RUN/NOT_OBSERVED；原资源未归因边界保留。
- 受控时序证明状态/输入/业务事实承接，不证明像素识别准确率。正式Inn上传探针也只是资源链验证，不是实机截图。
- 初次编译的测试variant使用/链接、模板v-else结构、长路径、source_path夹具、故障注入点及API夹具均曾失败；保留`native-build*`、`application*`、`web-build*`、`playwright-*`原始记录，不将失败擦成成功。
- API实际CAS契约是HTTP400加`PROFILE_CONFLICT`，不是夹具最初假设的409；改为合法内容配旧revision核对错误码和存储不变，没有修改生产契约。
- API fixture早先将保存按钮禁用当成保存结束，独立GET与保存后的目录刷新争用仓库锁，返回`WORKFLOW_REPOSITORY_BUSY`。现在等PUT响应、编辑锁释放和保存成功提示后再读；没有在生产层增加盲重试。
- 当前仓库对并发作者仓库操作仍采用既有BUSY语义，不在本包改为新的锁/排队架构；其它独立客户端并发读写的体验不由本轮顺序验收保证。
- 测试构建存在MSVC getenv弃用及终端颜色/LF换行警告，不影响本轮结果；未升级依赖或改全局环境。
- 工作包不授权部署，因而不能声称旧candidate32已经获得本轮修复。最终工作树保持未提交，等待单独commit/push或部署指令。

## 测试进程回收、未部署、循环未恢复

所有API/UI进程由`tests/closure-server.mjs`启动并记录PID、可执行路径、隔离数据根。服务重开仅针对自己的18755候选；从未调用现场停止接口。

最终以OS进程/监听只读核对自有PID及18754/18755，记录`E/process-cleanup-audit.json`。本轮自有测试进程已退出、两个端口无监听。没有结束现场17654或其它未知进程，没有连接设备，没有部署，没有恢复循环，也没有提交/推送。

无需运行现场回退。后续若放弃本轮代码，只能按本报告/manifest识别本轮改动逐项恢复；不得整仓reset/clean或覆盖`.vscode/`等用户内容。历史归档和失败证据继续保留供审查。
