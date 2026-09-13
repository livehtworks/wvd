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
