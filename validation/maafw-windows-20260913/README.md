# WVD × MaaFramework 独立 Windows 验证

这是固定版本的验证探针审核快照，不是 WVD 新版本，也不包含生产迁移。
**GPT 审核入口：[REVIEW_GUIDE.md](REVIEW_GUIDE.md)。** 本目录不参与 WVD 打包或运行。
先读 reports/FINAL_REPORT.md、reports/SCOPE.md、reports/EXECUTION_NOTES.md。
当前结论：NO_GO，内存斜率必选项尚未通过。

## 本机运行

以下命令面向另外准备好的独立验证工作目录，不是直接在此 Git 审核快照内运行。
本机原始验证目录已有 SDK、模型、样本及已编译 EXE；Git 不包含这些私有或可下载产物。
请先将审核目录复制到仓库外的独立位置、按依赖锁准备环境，然后执行：

```powershell
./scripts/build.ps1
./run_validation.cmd -NoPause -Case P2-NORMAL
./run_validation.cmd -NoPause -Case P2-STOP-NESTED
./run_validation.cmd -NoPause -Case P4-LIFETIME
```

不带 -NoPause 双击运行时会保留可见控制台；退出码保留。
默认只运行离线 P2-NORMAL，不会操作游戏。
单独 IMAGE 用例需要对应 fixture bundle；不能用空模型或零样本冒充通过。
长输出在 runs 下，命令原文在 private/commands.jsonl。
完整离线组可调用 scripts/run_offline.ps1；任一失败时非零退出。

## 分享包边界

分享 ZIP 不带 SDK/模型/完整仓库/真实截图/生产配置。
准备固定 SDK 时使用 scripts/prepare.ps1 -RepoRoot <已有WVD仓库>；不会切换或回退该仓库。
官方 SDK 与源码 URL/哈希、OCR 模型 URL/哈希列在 dependencies 下。
scripts/models.ps1 获取固定英文轻量模型；总模型约 11 MiB。
json.hpp 来源 nlohmann/json v3.11.3，MIT，许可证声明位于该头文件。

真实 WVD 样本只保留本机，分享包提供标签和哈希，不能无样本重现所有真实用例。
目录中脚本曾针对本次固定 run 的证据组织编写；跨机器需重新核实设备、采样并生成相应 case 目录，不能复制报告视为重新验收。
真实设备路径通过 private/target.json 和 private 的单次场景配置提供，分享包只给字段示例，绝不猜测其他设备。
实机执行前须重新确认停止和恢复安全门槛；不要自动执行冷启动脚本来关闭不明实例。

## 最小失败复现

在已准备好的本机目录运行：

```powershell
./run_validation.cmd -NoPause -Case P4-LIFETIME
```

该用例在一个原生进程中执行 5 轮预热 + 30 轮有限会话，每轮 100 节点。
本轮原始失败留在独立验证目录的 runs/P4-LIFETIME-c328fe0a。
Git 中可直接审阅 reports/memory_samples.json，保存了原始 35 行数值记录，仅从 JSONL 转成 JSON 数组。
报告记录了精确阈值，不应调宽阈值来消除失败。

来源：WVD 个人 fork 和 MaaFramework 固定提交，详见报告。参考代码保留相应 LICENSE，不包含绕过原项目贡献政策的上游提交。
