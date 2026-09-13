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
