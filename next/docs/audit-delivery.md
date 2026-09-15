# 显式清单审核交付

## 范围

这些标准库工具只读取源码和明确列出的证据，不构建、不运行 EXE、不发现设备、
不导入旧应用模块，也不执行 commit、push 或上传。审核包允许保留失败结果和未经
证实的历史身份；不允许路径越界、敏感信息泄漏、输入变动或哈希不匹配。

工具为 `next/tools/audit_snapshot.py` 和 `audit_package.py`，使用仓库 Python 加
`-B` 运行。除非另有说明，清单及命令参数中的路径均相对于仓库根目录。所有输出
写入 `next/.local` 下的新路径，不覆盖已有输出。

`source` 仅遍历 `next` 源码树，排除 `.local`、`.vscode`、`build`、`dist`、
`node_modules`、`webnode_modules`、`__pycache__`、`mod` 以及 SDK、模型和 Git
目录。**证据目录绝不递归扫描**：每个文件必须有独立清单项，不接受通配符、
目录型证据输入，也不会沿结果中嵌入的路径继续读取文件。

## 私有清单

`--spec` 接受 `next/.local` 下的私有 UTF-8 JSON 文件，或使用 `-` 从标准输入
读取。`repo` 必须是实际仓库根目录的绝对路径。清单包含路径映射，也可能包含
待脱敏的密钥原值，因此不得公开清单本身。

以下展示字段契约，不是最终证据清单。示例保留机器字段和英文占位符；实际使用时，
`repo` 填私有绝对路径，案例项替换为本轮约定范围内明确、完整的案例分母。

```json
{
  "schema": 1,
  "repo": "<absolute repository root, private only>",
  "baseline": "4d7c4fabd29bfadf55963bfdaecf79e3b6582bdd",
  "source_snapshot": "next/.local/review-final/source.json",
  "generated_build_id": "next/build/generated/core_build_id.hpp",
  "artifacts": [
    {"id": "workflow", "path": "next/build/m4/Release/test_m4_workflow.exe"}
  ],
  "artifact_snapshot": "next/.local/review-artifacts/artifacts.json",
  "path_aliases": {},
  "secret_values": [],
  "evidence": [
    {"id": "batch-log", "path": "next/.local/logs/completed-batch.log", "member": "batch/log.txt", "format": "text"},
    {"id": "case-execution", "path": "next/.local/completed-root/case/execution.json", "member": "batch/case/execution.json", "format": "json"},
    {"id": "case-result", "path": "next/.local/completed-root/case/output.json", "member": "batch/case/output.json", "format": "json"}
  ],
  "cases": [
    {"id": "batch/case", "batch": "batch", "reported_status": "FAIL", "report": "batch-log", "execution": "case-execution", "result": "case-result"},
    {"id": "batch/not-run", "reported_status": "NOT_RUN"}
  ],
  "protection": {
    "before": "next/.local/work/protection-before.json",
    "after": "next/.local/work/protection-after-summary.json"
  },
  "commands": [
    {"id": "batch", "argv": ["<PYTHON>", "-B", "-m", "unittest", "example.module.Test.test_case", "-v"], "cwd": "<REPO>", "exit": 1, "watchdog_seconds": null, "evidence": "batch-log", "provenance": "recorded by batch owner"}
  ]
}
```

- `artifacts` 必须逐项列出**拟纳入最终审核身份范围的所有当前 EXE**。只读取其身份，
  不将可执行文件字节装入 ZIP，也不从目录内容自动推断 EXE 清单。示例只列一个，
  不代表最终集合。
- `evidence[].format` 为 `json`、`jsonl` 或 `text`；`member` 是公开包中
  `evidence/` 下的相对文件名。可选 `sha256` 固定原文件字节身份。重复 ID、
  重复成员名和 Windows 大小写冲突均拒绝。
- RunStore 持久化结果、SDK 模块记录、依赖及模型锁元数据、历史构建身份、
  结构化成本和失败记录分别作为显式证据文件列入；不导出模型或 SDK 二进制。
- 缺失或 JSON 无效的证据记为 `UNVERIFIED`；有原字节时记录其哈希，不把无效
  原字节转换后冒充安全文本导出。
- `reported_status` 是批次负责方报告的结论，不是导出器重新执行测试的判断。
  `cases.json` 分开记录 exit、实际状态、计数、quiescent、mismatch 和产物哈希
  匹配。负例的 Failed 状态不等于业务成功，exit0 不生成 PASS，null 不补成确定值。
- 命令只保存已有 argv/cwd/exit/watchdog 记录，工具不执行它们。缺失值应为
  null/UNVERIFIED，不根据当前测试源码猜测补填；命令记录的支持证据也须单独列出。
- 保护摘要**只从指定记录派生**。导出器不打开450个被保护文件、config、mod 或任何
  用户数据。已有 `changed`/`production_writes` 保留其报告来源，不代表本轮重新测量。

## 源码与构建身份

`source` 对固定基线 `4d7c4fa` 执行只读 Git 操作：`ls-tree`、`cat-file --batch`、
`ls-files`，不修改工作树或暂存区。源码快照包含**当前被接纳的完整文本源码清单**，
不只是差异：逐文件记录原始 SHA-256、字节数、基线 SHA-256、已跟踪/未跟踪状态和
字节级变更。基线中已删除的源码也有记录。CRLF 差异在这里是实际字节差异，不代表
代码语义改变。未分类文件明确列出；非源码图片、二进制及私有 config/input 文件排除。

包内载荷包括新增/修改的文本源码及锁文件、构建描述；未变文件仍列入完整身份清单，
可从固定基线取得。不导出含未脱敏字符串的原始 Git 补丁。`delta_sha256` 标识规范化的
变更/删除清单，不是可执行补丁的哈希。

工具按 `next/native/maafw/CMakeLists.txt` 当前规则独立计算 CMake 核心身份：
native 和 tests/native 下的 `.cpp/.hpp`、该 CMake 文件、dependencies.lock.json
及可选的 opencv.lock.json。Python、Web 和文档由完整清单覆盖，**不由范围较小的
核心身份覆盖**。以后 CMake 身份规则变化时，也须核对工具的 `core_id` 函数。

`artifacts` 记录 EXE 哈希及生成头文件的精确哈希/build_id，报告完整源码身份自构建前
快照以来是否保持不变、当前核心源码是否匹配生成头。**两者都不能单独证明 EXE 与
源码的构建绑定。** `source_binding` 默认为 `UNVERIFIED`。

清单中可选的私有 `build_record` 路径可引用构建负责方已有记录，其中包含
`source_inventory_sha256`、`exit`、`build_id` 和 `exe_sha256`（产物 ID 到 SHA-256
的映射）。仅在记录匹配时产生 `RECORDED_BUILD_MATCH`；这不是工具亲自执行构建的
密码学证明。原构建记录、实际命令和构建日志都应列入显式证据清单，不能用新观察到的
哈希伪造成功回执，再据此提升旧构建的身份结论。

历史结果保留自己的 execution 哈希。匹配当前 EXE 时只产生 `EXE_HASH_MATCH`，案例
摘要中的历史源码绑定仍为 `UNVERIFIED`。已有更强历史记录继续作为证据保留，不重新
绑定到最新源码快照。失败或 `UNVERIFIED` 记录不阻断 ZIP 生成。

## 脱敏与安全

- 读取根受控：源码位于 `next`，证据位于 `next/.local`，当前产物/生成头身份位于
  `next/build`。拒绝符号链接和 Windows 重解析点，不沿 JSON 值中的用户目录或
  SDK 路径继续读取。
- 在 `path_aliases` 中明确列出私有绝对路径前缀映射，使用 `<SDK>`、`<USER_HOME>`、
  `<TOOLS>` 等占位符；`<REPO>` 自动加入。最长前缀优先，支持正斜杠、反斜杠和
  转义反斜杠形式。
- JSON/JSONL 按结构解析。profile/config/environment/export/values/私有输入
  容器及已识别的凭据、设备字段整体替换；被移除 profile 内的未知字段不会泄漏，
  其他字段保留业务含义。
- 私有 `secret_values` 可列出密钥原值，每项至少四字符。已识别的密钥赋值和命令
  选项会脱敏；疑似凭据 token、私钥、未映射盘符/UNC/常见本地 Unix 路径或含凭据
  URL 会阻断导出。文本日志中出现 Python repr 形式的 profile 也会阻断，应提供
  单独审核的结构化摘要，不通过放宽过滤解决。
- 文本源码脱敏可能改变字符串字面量，原始哈希和脱敏哈希刻意区分。接收方需配置
  合适的本地占位符值才能复现命令，不能声称 ZIP 与私有构建输入逐字节一致。模式
  过滤无法保证识别任意未知密钥；清单负责方仍须审核自由格式源码/日志并补充私有值
  映射，不能把整份日志或整个根目录加入白名单来消除泄漏报告。
- EXE/DLL/SDK/模型/图片/config/mod/input 原件均不得作为载荷。只接纳文本源码和
  明确允许的证据格式。固定哈希不符、当前 EXE/生成头变化、采集或打包期间输入
  变动均报错。

## 冻结步骤

源码负责方仍在编辑或原生证据仍在生成时，不得运行 `pack`。以下命令用于主代理
下一次受控冻结，不构成工具开发期间自行构建或打包的授权。

1. 先按已有授权流程准备所有生成源码/数据。冻结源码写入者，再采集完整构建前身份：

```powershell
.venv-build/Scripts/python.exe -B next/tools/audit_snapshot.py source --spec next/.local/review-spec.json --out next/.local/review-before
```

2. 主代理执行另行授权的构建，记录实际 argv、exit、日志及生成源码变化。工具本身
   不执行构建；期间不编辑源码，随后采集构建后源码与产物身份：

```powershell
.venv-build/Scripts/python.exe -B next/tools/audit_snapshot.py source --spec next/.local/review-spec.json --out next/.local/review-after
.venv-build/Scripts/python.exe -B next/tools/audit_snapshot.py artifacts --spec next/.local/review-spec.json --source next/.local/review-before/source.json --out next/.local/review-artifacts
```

3. 比较前后 `inventory_sha256`、产物/生成头观察值和实际构建记录。生成源码差异
   必须报告，不追溯改写构建前身份。测试只在主代理租约下运行，并在清单中逐项列出
   已完成证据文件和实际方法分母。全部源码/文档最终冻结后，用相同 `source` 命令
   和新输出目录采集 `review-final/source.json`。

4. 私有清单中的 `source_snapshot`、`artifact_snapshot` 指向选定的最终记录，再执行
   检查。允许保留失败和未经证实的历史证据：

```powershell
.venv-build/Scripts/python.exe -B next/tools/audit_package.py check --spec next/.local/review-spec.json
.venv-build/Scripts/python.exe -B next/tools/audit_package.py pack --spec next/.local/review-spec.json --freeze next/.local/review-final/source.json --out next/.local/review-delivery/review.zip
.venv-build/Scripts/python.exe -B next/tools/audit_package.py verify --archive next/.local/review-delivery/review.zip --receipt next/.local/review-delivery/review.receipt.json
```

`pack` 发出成功回执前会复核完整源码清单和每个已读输入。ZIP 的时间戳和成员顺序
确定；`INDEX.json` 覆盖全部载荷成员的原始/脱敏哈希，外部回执覆盖索引及整个 ZIP，
避免循环自哈希。`verify` 直接读取包内成员，不解压；成员缺失、多出、重复、路径
不安全或哈希不符均拒绝。若 ZIP 创建后中断或发现变动，没有核验回执的 ZIP 视为
不完整，不得交付。重试使用新的输出名。

CLI 成功退出码为0；不安全/无效输入退出码为2，错误信息不回显私有值。
`check` 成功仅表示打包检查通过，不表示测试通过。

## 隔离测试与当前证据

```powershell
.venv-build/Scripts/python.exe -B -m unittest discover -s next/tests -p test_audit_delivery.py -v
```

所有测试数据和测试 ZIP 均在可丢弃的 `TemporaryDirectory` 夹具中创建。假 EXE 只是
用于哈希的惰性字节串；这些测试不运行 Git 或原生进程。仅标准输入及平台链接元数据
采用替身，源码采集、脱敏、证据收集、打包和校验均执行工具真实代码路径。

纯 Python 隔离自检29方法通过，0失败、0错误、0跳过；日志及四文件当时哈希位于
`next/.local/audit-delivery-tests-2we6iyi7/unittest.log`、`result.json`。该次哈希
对应翻译前文档，不用当前文档哈希追认当时自检。真实 Windows junction 的创建/拒绝
仍未实测，未知敏感信息仍需按前述边界人工审查。

`007a7d6` 构建后，主代理已实际运行 Git `source` 和 `artifacts` CLI。本次翻译前
只读核对了以下记录，不重新运行工具、构建或 native：

- `next/.local/m4-final-source-diagnostic-call-fix/source.json`：403个源码文件，
  `inventory_sha256=c123f8e45643e9d2547cb6c9fd45633f70339831afdae8d8b5a6f72b643a8d54`。
- `next/.local/m4-final-artifacts-007a7d6/artifacts.json`：16个 EXE；前后源码身份一致，
  `generated_matches_current_core_sources=true`，`source_binding=RECORDED_BUILD_MATCH`。
- `next/.local/m4-final-source-diagnostic-call-fix/build-record-verified-targets.json`：
  exit0、16个 EXE 身份，记录哈希与 artifact 快照引用一致；build_id 为
  `402549e5b4f6e84138d0d6d7f1992bcbaf8c850227e4242008f4a2168064bf67`。

最初误列不存在的 `test_m4_plan` 所致失败不作为成功证据，以上采用修正后的显式目标
清单记录；本次不删除或改写旧失败记录。CLI 已跑通不等于最终审核包已验收。

**最终审核 ZIP 尚未在本轮实际生成并核验。** 本次仅翻译文档，不修改工具源码。
翻译会改变完整源码清单的文档哈希，但不改变 CMake 核心身份覆盖的文件；保留上述
构建时快照，日后若决定生成审核 ZIP，须在文档再次冻结后新采交付源码快照，不能
改写历史快照。本轮只做公共能力必要回归，全游戏任务矩阵不作为打包或阶段收口前置。
