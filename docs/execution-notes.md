# 执行注意项

## Windows 与构建

- 中文源码和文档使用 UTF-8。PowerShell 读取时显式指定编码；Git 的 CRLF 提示不等于文件损坏。
- 本地打包入口：`用于本地测试的打包脚本.bat`，独立环境为 `.venv-build`。自动执行时设置 `NO_PAUSE=1`。
- 构建输出应写入临时日志，完成后检查退出码和 `[INFO] Build completed`，不能仅凭 exe 已存在认定成功。
- 发布目标为 `dist/wvd`。构建前后核对 `config.json` 哈希；保留日志、mod 和未知运行文件。
- 只检查发布目标下的 exe 是否占用；另一目录运行的同名 wvd.exe 不是本次构建进程，不应终止。
- GUI、导入和日志验证必须在本轮独立临时目录运行。`utils` 导入会初始化日志并执行保留策略，GUI 可能保存配置，不能用正式运行目录做 Smoke。
- 构建环境可能输出第三方 `ppadb` 的转义序列 SyntaxWarning；需以实际异常、退出码和运行验证判定。
- 全资源解码时部分历史 PNG 会输出 `sBIT: bad length`；本轮图片均可正常解码，不自动重编码这些资产。

## 同步上游

- `origin` 是原作者仓库，`fork` 是个人维护仓库。推送使用明确的 `fork` 和当前分支名。
- 不直接用上游文件覆盖本地截图、战斗、重启、GUI 日志与配置保护逻辑。
- 本次同步基线与功能取舍见 `upstream-sync-2.8.7.md`；当前事实见 `project-status.md`。
- 上游将匿名角色模板重命名，会影响既有策略中的角色名；当前保持原模板名并新增忍者。

## 独立验证审核

- `validation/maafw-windows-20260913/` 是独立验证的审核快照；复测应在仓库外建立可丢弃目录，不直接操作生产配置或启动生产任务。
- SDK、模型、真实截图、设备配置和运行日志不纳入提交。实测结论、数值数据与原始运行日志分开维护；缺少私有样本时不得把完整验收算作通过。
- 中文路径需使用探针的进程级 UTF-8 manifest；MuMu 管理器报告 Android started 后还应有界等待 ADB 和 boot property。详细已知问题见验证目录的 `reports/EXECUTION_NOTES.md`。
- Maa 5.13.0 的 Context 子任务失败可返回有效 ID，须查 TaskDetail；子动作回调又可能携带根 task_id，根终点还须核对 generation 与嵌套层级。M0 实测依据见 `maafw-m0-review-resolution.md`。
- MaaImageBufferSetEncoded 返回真不保证图像非空，必须继续检查尺寸、通道和像素；负例不能把识别 Error 当 NoHit。停止输入计数必须先于门禁。
- 固定资源诊断只执行约定轮数，不循环复测挑 PASS。HeapWalk/线程入口不等于完整活分配栈；证据不足保留 RESOURCE_UNRESOLVED，不据此切换生产。
- M0 合成 OCR 正例仍为 outcome=Unset：只验证调用和流程完成，不能把模板三态结论推广到 OCR。正式识别适配在 M2 根据 OCR 详情/错误落实三态，不通过根任务 Completed 反推识别结果；该缺口不要求重开 M0 资源诊断。

## Next 独立 M1 工程

- 构建/验收入口为 `next/tools/build.py`、`next/tools/validate.py`，只写 next 构建/测试目录及专用依赖缓存，不打包旧 wvd。工具链版本见 `next/dependencies.lock.json`。
- CMake 未加入 PATH 时通过 VS Installer 的 vswhere 发现 VS 2022 内置 CMake，不硬编码个人安装路径；Web 使用锁文件 `npm ci`，不安装浮动版本。
- automationd 运行期间重新链接会出现 LNK1104。先 Ctrl+C 正常停本目录服务再构建，不全局结束同名进程。验收工具使用独立隐藏控制台发送 Ctrl+C；强杀只用于失败测试的专属进程清理，不能计作停止通过。
- Windows bat 为 CRLF、部分历史文本混合换行；比对 Git 基线时用 Git 过滤后的 blob 核对内容，不能把 checkout 换行差异当作源码被改。生产 Python 另保留基线 SHA256。
- 盘点必须同时扫描 if 和 match/case、无 command 的可操作控件、bind_class，以及策略嵌套字典字段；不能只数函数/按钮就宣布完整覆盖。
- 改盘点规则后先生成再构建 Web 静态镜像，否则原生服务会托管旧报告。验收会逐字节比较服务返回的 JSON 与盘点源产物。
- 浏览器验收安装固定 Playwright 的 Chromium，使用原生同源服务；`NO_COLOR/FORCE_COLOR` 提示不等于测试失败。截图与结果只留 `next/web/test-results`。
- HEAD 必须在实际发送边界统一去除正文，包括提前返回和异常响应；以原始 TCP 数据核对正文为 0，不能只用会主动隐藏 HEAD 正文的 HTTP 客户端证明。
- 清单刷新保留正在使用的筛选条件；按响应应用时的 selectedId 重绑新对象，并裁剪有效页码。不能捕获请求发出时的选择覆盖用户在等待期间的新选择。

## Next M2 离线核心

- 只在 `prepare_maafw.py` 校验固定 SDK/模型后用 `--m2-offline` 显式构建和验收；未准备不是跳过测试的理由。默认构建明确关闭该 CMake 选项，避免继承缓存 ON 状态。
- M2测试组用现有`unittest discover -s next/tests/m2`入口。test_runtime按该目录导入test_recognition，不用仓库级dotted模块名混跑，否则运行组会在加载时失败；这属于调用错误，不能算核心测试已执行。
- 专项夹具可能在图像合成自检或发布校验阶段失败，此时没有原生Run结果，不能把缺少snapshot写成核心崩溃。先核对具体异常、EXE身份及零连接/输入。focus_cursor的中心差异小于0.2表示仍与模板相同；在map_target中“不再匹配焦点”才是已到达，不可提前合成已到达状态。
- SDK/模型缓存、完整日志和合成样本在 `next/.local`，不提交私人路径或二进制。M1 服务不链接离线识别库；不能根据离线测试通过而连接游戏。
- Maa 5.13.0 的 `MaaSetGlobalOption` 会先向 stdout 输出弃用日志；使用 `MaaGlobalSetOption`，结构化测试结果单独落盘，不混用 SDK stdout 作为 JSON 协议。
- C++20 中文路径用 `std::u8string` 构造 filesystem::path，配合进程 UTF-8 manifest；不使用已弃用的 u8path，也不关闭警告掩盖问题。
- `bcrypt.h` 和 `objbase.h` 必须在 `windows.h` 之后。将其单独 include 分组，防止 clang-format 字母排序导致基础类型缺失；不修改系统头文件或关闭编译检查。
- 独立识别不绑定 Controller；完整运行测试绑定真实 Maa CustomController，但具体设备仅为测试离线实现。每轮新目录包含合成资源、配置和专属 run-data，不连接实机。测试外部超时只算失败，不能作为生产停止方案。
- 必须等构建进程退出并确认成功，才能启动该 exe 的测试；边编译边测试会导致 LNK1104 或误跑旧产物。本轮发生过一次，相关混合轮次不计入验收；测试增加了 exe 哈希核对。
- Maa 5.13.0 完成部分识别/清理时会额外调用 Inactive；Scroll 会先尝试 TouchMove 定位光标，即使失败仍调用 Scroll。门禁必须记录并拒绝未授权调用；逐动作断言用调用前后增量，仍要求底层未授权输入为零。
- 过期帧测试从真实 captured_at 加策略有效期等待，不能将有效期缩得比识别耗时还短、然后把准备阶段偶发失败当作过期准入测试。
- 坐标映射合成样本为模板外围保留平滑边缘，避免两次缩放将随机背景混入模板。阈值仍为 0.99；该测试仅证明映射链，不证明真实 NEXT 小尺度或遮挡识别。
- STOP_TIMEOUT 后仍保持原生对象与设备租约；工作线程真实返回并释放按住输入后才报告 quiescent。不要用 kill/detach“解决”测试等待或伪造正常停止。
- M2 的结果与终态事件共同提交到 result.json；events.json 只是较早的活动诊断快照。历史审查优先读 result.json，数据权威详见 `../next/docs/data-authority.md`。

## Next M3 受限验证

- `--m3` 只构建或运行离线测试；设备检查必须另行显式指定私有绑定。固定 OpenCV 4.12.0 来自 MaaDeps v2.12.6，与 Maa 5.13.0 的 DLL hash 相同，不安装第二份运行时。
- 固定 Maa 的 CustomRecognition 详情位于 `all[0].detail`；模板的 filtered/score 结构不能照搬。实际 Pipeline 的入口节点不一定先识别自身，应通过 next 候选验证真正调用了自定义识别，不能只看任务成功。
- 固定 Maa ADB 截图重试内部会调用 KillServer。M3 通过公开 command.KillServer 覆盖为目标设备 get-state，禁止全局服务重启；内部重试和阻塞等待仍保留，不能写成已彻底禁用 SDK 自动重连。
- Maa 原始图像指针为 void*，构造只读 BGR span 必须明确转换类型并验证尺寸/通道。C++20 与固定 nlohmann JSON 比较字符串时显式 `.get<std::string>()`，避免 MSVC 重写比较运算歧义。
- 动态临时 JSON 不得被 AssetResolver 保存为引用；别名表由解析器持有自己的不可变值。不同 Bundle 同名资源必须按 revision 与规范路径隔离。
- M3 真设备检查遇到未知场景不打开 VPN/游戏、不试点找按钮。系统导航没有前置安全证明时单列 BLOCKED；真实截图不得冒充 NEXT/Pause 质量样本。
- Android 15 的 `dumpsys window windows` 子段可能只有窗口列表而无 mCurrentFocus，前台观测应使用完整 `dumpsys window` 并验证焦点字段。不能将字段缺失当成默认游戏前台；本轮实际导致安全 Failed，修正后通过。
- 模拟器启动后出现过一次未知 Python 控制者阻断，具体进程身份未归因；保留严格拒绝。后续排查应记录被拒 PID/来源，不可直接加宽白名单或杀未知进程。

## Next M3 修复与 M4 数据验证

- 用户已明确要求当前工作包每阶段做好本地commit，并在最新范围修订中授权推送个人fork现有分支；不含向原作者提PR、发行或生产切换。全游戏任务矩阵不再作为本轮收口前置，历史失败不改写成通过。阶段提交记录实现范围、验证证据和未完成项；不能自行以局部提交替代用户仍要求的剩余工作。
- MetadataQuery 首次启动自有 helper 后，进程总句柄可能增加 2 或 5 个；单独 CreateProcess 对照也曾增加 5 个，但两者不一致，不能据此扣掉差值宣布通过。两轮修正后已保留失败；不要反复跑同组挑 PASS。待定来源及收尾所有权问题见 M3 修复报告。
- 本轮没有真实设备授权。不得运行设备发现脚本或查询已开的 MuMu/ADB；异常注入只针对自有 metadata helper。旧 Gate B 与新 EXE 不同，不能沿用旧通过记录。
- Custom 识别的 boolean-only Hit 可用于场景/后置条件，但目标坐标动作必须另有合法位置；低置信 NEXT 不直接授权。测试应观察真实后置帧，不以截图次数自动推进离线场景。
- RunCoordinator 的 quiescent 可以先于结果文件提交出现。测试需要等待 `quiescent && (result_saved || storage_error)`，不能读到尚未保存的中间快照就断言业务终态。
- 资源完整性改为活动快照：活动写入应被 Windows 共享锁拒绝；作者副本变化不影响现有 Run，但新加载要拒绝旧 hash/复用 revision。旧“修改活动源后必须 Error”测试按此契约更新，不得干脆删除。
- BundleLease 封存需要额外文件句柄、字节缓存和磁盘副本，不能只报告热路径变快。当前未实现自动快照清理；不要自行删旧源、配置或已有日志。
- M4 导入只接受明确的新目的目录。`wvd_m4_check` 不连接设备也不执行任务；完整任务通过数仍为零。CAS 草稿必须含完整 33 字段，不用解析默认值替代缺字段的保存验证。
- 原生用例必须在调用前后核对EXE哈希，并在检查退出码之前保存execution.json；否则失败分支会丢失产物身份。只有旧日志PASS而没有当时哈希的结果，保留身份未证实，不能用当前文件补造历史证据。
- 纯视觉/默认对话探针若没有业务检查点，不应无条件绑定业务状态；带业务状态却缺检查点被BUSINESS_UNIT_INVALID拒绝是正式校验正确生效。坏图片负例必须断言具体解码错误，不能把任意启动错误算作识别器拒绝坏图。
- 资源manifest按精确大小写闭包，Windows文件系统能打开错误大小写不代表引用有效；Fordraig跳跃图的规范名是fordraig/Leap.png。修正作者引用与夹具，不通过新增大小写别名隐藏错误。
- 最终 build 退出后再测试；C++ 格式化与源码变更会改变 build_id，之后需要重新构建。修前/修后固定窗口各 5 次预热、30 次正式，只执行约定窗口，不追加预热或测试量解释未决内存问题。

## Next M4 状态与任务数据

- 因果流程夹具的拒绝注入在单个预期command里设 `reject=true`；没有 `reject_after_first` 开关。未触发真实拒绝时不能将正常执行结果算拒绝测试。无恢复策略的Run可能归一为 `RECOVERY_REQUIRED`，具体业务原因核对 `sessions[-1].reason`。
- 原图片来源优先级是基础图、基础别名、mod；测试mod必须明确让该基础成员缺席，并核对发布后的 `image_sources`。往mod放同名图不代表测试已经使用它，不能为使测试通过而反转正式优先级。

- Windows合成图片按封存别名规范化后去重，例如 `returntoTown.png` 指向 `returntotown.png`；不能同时写仅大小写不同的文件后再声称两个成员均存在。生产资源闭包仍按精确manifest校验。
- 固定OpenCV4.12的嵌套 `parallel_for_` 会将内层串行执行；含默认对话四路识别的复合模式不再放进外层两路并行白名单。此修正不能证明全部慢帧已归因，巨人专项两轮修正后仍保持阻断。

- 完整迭代计划测试一次编译/校验/序列化 43 份图；普通提示子图增加后曾超过原 30 秒测试进程看护。该批次看护为 120 秒，保留全部逐项断言；它不是运行 Session 预算或性能验收阈值。超时原轮保留 ERROR，不当作业务失败或成功，见 m4-global-prompt-validation.md。
- EventJournal 是有界窗口，result.json 中的事件列表不保证包含早期每一次输入。核对全程次数用 Run 总计与 sessions[].inputs，核对仍在窗口内的输入用 session_generation；旧结果缺逐代次计数时不得补零。冷启动首轮曾错误地要求窗口保留全部 8 次输入，见 m4-cold-start-validation.md。

- 死亡提示首轮后置识别曾耗时 2244ms，超过 2 秒 TTL 并被 SCENE_UNCONFIRMED 拒绝。应先用 frame.captured 与 recognition.custom 事件时间定位，再收敛专用有序候选和重复计算；不延长帧 TTL，也不改通用 any/all 的错误传播来通过测试。见 m4-party-death-validation.md。

- Maa 5.13.0 RunActionDirect 的参数只进入 action.param，不能用它设置节点 pre_delay；会继承默认约 200ms 输入前延迟。受控 Click/Swipe 通过 RunAction 的本次克隆节点显式设零前后延迟及冻结等待，外层业务等待保持原定义；不要放宽 2 秒 TTL 掩盖重复等待。详见 `../next/docs/m4-native-action-timing-validation.md`。
- 前后延迟已设零之后，不能再把约200ms残差归为同一个默认等待。固定SDK的Direct识别不经过Pipeline失败轮次rate_limit，普通Click也无另一个固定200ms等待；Context克隆/override校验会合并检查整包Pipeline，是大图残差的候选来源，尚无分段计时证明占比。帧龄必须从frame.captured到识别回执/实际输入分别核对，不能只减matchTemplate耗时后断言原因。
- `CompiledWorkflow.time_limit` 必须发布到 Session 并参与定义身份；长组合段不能继承短子流程默认预算。内联/原生子调用不重启父总计时，帧 TTL 和停止预算独立保持；测试等待依据实际有限 Run 定义，不用外层 watchdog 代替正式停止。
- 新业务摘要字段必须同步核对 `business_condition` 的显式白名单。业务条件是只读选路信息，`action_eligible=false`；WvdConfirm 不能把业务条件和视觉条件混为许可，应先 observe 选路，再用新帧视觉确认，并在状态所有者中再次验证业务前置。角色恢复首轮曾分别因遗漏白名单和混合确认被安全拒绝，详见 m4-healing-validation.md。
- 新确认事件须同时进入 `PipelineCompiler::confirm` 事件白名单和 `WvdRunState::confirm_event` 消费者，不能只测直接状态方法。复活接线首轮漏登 `revival_observed`，所有流程在连接前拒绝；详见 m4-revival-validation.md。
- M2/M3/M4 离线夹具可能使用相同设备身份，Windows 设备租约仍是跨进程互斥。不同临时数据目录不代表可以同时运行这些组；必须等上一组进程完成再启动下一组。曾重叠运行 M3 与 M4 状态组导致 DEVICE_BUSY，保留该轮证据，串行补验；不能关闭租约或把这个拒绝当产品崩溃。
- Maa 5.13.0 的 Context 子任务共享节点命中计数，不能用“新 Task ID”推断 max_hit 已重置。编译器为有限原生子调用封存精确的局部节点清单，经 ClearHitCount 清理局部次数；父预算、业务策略和观察许可不清理。子图作用域与最长调用深度都须发布前验证。
- 新专项的共用Stage路由须按实际阶段数设置有限max_hit。默认五次会截断第六阶段，并落入通用budget_exhausted；该原因不一定是墙钟超时，应核对最后节点与已确认phase。7000G首轮复现于Confirmed4之后。
- 单个json数组值用大括号构造可能复制该数组本身，而非创建一层外部数组。单点路线须用`J::array({point})`明确维度；牛洞ACTIVE_REST=true分支曾因此TASK_TARGET_ARGUMENTS，而false分支可编译。
- 固定JSON库混合有符号/无符号数直接比较不能作为int64范围证明。先检查`is_number_unsigned()`并显式读取uint64做上界检查，通过后再转换；崩溃阈值曾漏拒uint64最大值，`7fa2957`修正并通过状态负例。不改变既有负数阈值语义。
- 因果输入夹具的Swipe须显式记录duration；默认0不能匹配正式400ms滑动。世界跳转到地下城画面后，打开地图是一次独立真实输入，不能直接给mapFlag跳过它。错误夹具轮次保留，不放宽正式后置条件来迎合测试。
- 嵌套 GuardedAction 的底层失败不能只返回 false：SDK 可能先转 on_error，将其误归为可重试恢复。非取消输入失败显式报错；普通测试子动作 false 仍按真实 TaskDetail 判断，不统一提前写入失败。
- 启动就绪候选包含许多模板，按通用 any 全部计算会使后置帧过期。WVD boot_ready/boot_post 保留旧判断顺序，遇首个命中即结束，并从同一 probe 定义收集隐式资源；不放宽 2 秒 TTL。实际执行过的探针 Error 不作 NoHit。
- 恢复升级不能只看上一 Session 是否带 lifecycle：原任务已恢复后可能再次故障。以业务摘要中的活动恢复标志区分同次升级和新请求；启动成功必须同时证明进程运行和前台正确。
- 复合视觉条件不要在同一 evaluate 内重复计算相同子表达式；局部 memo 必须保留 ROI/预处理身份和深度校验，不跨调用/帧/代次。入本模板动作不应每次匹配整条路线所有页；实测识别约 1.86 秒加 SDK 动作等待约 0.21 秒会越过 2 秒帧 TTL，应收敛场景证明，不能延长 TTL 掩盖。
- 状态工厂必须在 BehaviorRegistry seal 前注册，函数非捕获；其参数进入 definition_version=3。Maa 回调可能另在线程，状态修改通过 Context 的有锁访问，不能依赖线程局部变量保存整个 Run 状态。
- 正常有限段需要本根任务检查点和真静止，不能复用 RecoveryRequired。单调时间起点用可空值；测试从零开始也应正确累计。
- 旧 TargetInfo 的第三个参数并不总是 ROI：position/stair 为点，harken/Bharken 可以是楼梯资源名。旧 EOT 和嵌套 fallback 是顺序列表，不可直接作为 Maa 候选 next。
- m4_inventory.py 使用本轮 data/state/plan 证据更新叠加表；implementation_status 与 implementation_extent 分开，纯数据 PASS 不填写任务 offline_status=PASS。未实现专项仍保留在全部 58 项分母内。
- M4 有限业务测试使用发布产物的 Session 时间预算；不要在测试驱动另设较短总限时后，又要求走完更长的候选重试链。无恢复策略的 RecoveryRequired 按现有契约返回 Interrupted，不是 Failed 或 Completed。
- 多轮陷阱流程的外层看护需随 normal_units 计算，不能沿用单轮预算截断第二轮；只调整隔离测试看护，不改变生产 Session/帧 TTL/停止预算。发现时正在运行的旧轮次保持原样，不改写结果。
- 固定 SDK 提供的 JSON 头是 `<json.hpp>`，不是 `<nlohmann/json.hpp>`；新游戏模块沿用现有 include，不另装第二份依赖。
- Maa 节点未提供 custom_action_param 时，现有 Context 可传入 JSON null；给 RequireRecovery 增加可选原因时须保留这一无参数契约，不能直接对 null 调用 value()。
