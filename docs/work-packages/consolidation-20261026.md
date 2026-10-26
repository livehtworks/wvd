# WVD 工程收束落地方案与推进计划

日期：2026-10-26，Asia/Shanghai。审查提交：179cfd851877cba2f2df401d86a905ff9ae088f6。盘点历史基线：205692c7bdea73634e780f1af0c70eef4fa62d0c。适用仓库：livehtworks/wvd，当前个人修复分支 codex/t0-lifecycle-repair-20261025。

**出包时状态：方案与文档修订已准备。用户随后已授权执行工作包及对应盘符trash迁移；当前执行结果只在[任务登记](consolidation-20261026-tasks.json)维护，摘要见[执行报告](../reviews/consolidation-execution-20261026.md)。T0保持OPEN。** 本文件固定范围、方法和验收定义，不替代具体现场与外部动作授权。已有会话授权按真实范围继续有效，不为同一已授权动作重复增加确认。原事故报告、原始证据、既有架构和用户决定继续保留。

## 1 目标与不变边界

本轮目标是把个人维护工程收束到入口清楚、变更可追溯、产物有去向、验证可复用、后续执行可验收的状态。工程整理与生命周期整改分别交付；任务完成状态不能用“计划已写”“目录变少”替代。

- 当前产品仍在 next/，C++20/CMake、Vue、OpenCV、ONNX Runtime 技术栈保持。
- Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor 是唯一执行链。发布与资源管理仍由现有 Application、NativePublisher、BundleLease、Coordinator、RunStore 承接。
- profile.json、workflows/ 的正式数据权威、CAS、业务成功断言、请求幂等、Unknown 输入和未结副作用保护保持。
- 旧 src/、config、mod、根 dist、未提交修改及必要诊断是受保护内容；Next 已有替代不自动证明旧入口可退役。
- 不增加外部循环/清理守护器、独立数据库、另一套数据权威或全新管理平台；不修改游戏流程、识别阈值、设备/模拟器设置。
- 上游同步、独立未解导航/内存问题和其他项目不并入本轮。发现相关问题只记录证据及依赖。
- 本方案提出的内部资源布局和计量改动是拟实施设计。旧合同继续生效，只有兼容验证和对应源码交付完成后才更新“现行规则”。

## 2 已确认事实与未验证项

### 2.1 直接核查的 GitHub 事实

截至本次核对，个人修复分支 HEAD 仍为179cfd8，6个变更路径都在docs/下。源码未因这次报告提交变化。当前manifest为593成员、591张PNG；24条source指向不存在的根来源，实际Next来源与包内图Git blob相同；295条source_refs为空或缺失，其中38张仍被当前语义配方直接使用。295具体为274空数组、21缺字段，38具体为17+21。

源码已确认：内部repeat/handoff不逐轮登记同ID requests；已接管完整发布目录没有历史退出；next文档变化会被select_modules兜底选成全模块；master旧工作流会匹配旧Python发布；原生合同工作流没有push触发；当前打包不携带PDB。详见第10节固定提交证据。

### 2.2 盘点报告提供的本地快照

2026-10-26盘点记录主工作区1,911,793文件、292.644GiB逻辑量，正式published有1,334份请求快照。模型4,397个路径对应2,272个不同文件对象；逻辑84.152GiB、FileID去重内容33.846GiB。全仓实际分配与可回收量未知。另一工作区17个未提交路径；正式41份配置与旧发布快照一致。新200目标已核39份成功，run40为UserStopped且排除，循环停止、监控PAUSED。

这些是有时间点的报告事实，不是本包重新访问D/S盘得出的实时结果。执行者以原证据SHA核来源，再只补本轮需要的现场变化。旧PID不作为后续操作依据，配置新变化先按用户编辑保护，不自动恢复成旧哈希。

### 2.3 当前保护额度和待补证事项

| 当前控制 | 已实现口径 | 本轮不得外推的结论 |
| --- | --- | --- |
| 准备准入 | data卷available至少2GiB；发布另加首次物化及事务最坏成本 | 不是预分配，也不证明外部程序不会抢占空间 |
| 批次额度 | 单watcher累计runs与published路径逻辑字节16GiB；续轮另预留2GiB | 不是跨批历史总额度，硬链接不会降低旧逻辑计数 |
| 请求历史 | 65,536条、256MiB及既有扫描/原子写保护 | 内部repeat不逐条入requests，因此不能限制全部published |
| 最近帧 | 240张/128MiB、pending4及in-flight1 | 不包含所有正式诊断和历史资源 |
| 目录事务 | 旧previous保留并参与以后验证；全次inventory有界拒绝 | 达上限拒绝不是历史生命周期已完成 |

按报告136.31MiB样本，200份完整发布树约26.62GiB，还未加runs；这是带前提的预算风险，不是当前批次已测大小，更不是run40停止原因。C07须核当前任务真实成员与字节，C08须使既定200目标和新计量兼容；不足时报告真实冲突，不改业务目标或绕额度。

未核实项目：17处修改完整内容、当前实时服务身份、每个历史目录所有者/引用、配对PDB GUID+Age、当前200批每轮资源尺寸、全盘实际分配/可回收量，以及A2所有物理路径消费者。未知值保持null/未测，不充当0或删除依据。

## 3 推进顺序与阶段出口

本计划按依赖安排工作段，不编造固定工期。每个执行会话结束记录实际完成、失败、产物和下一项；无需把每个工作段变成人工重复审批。达到阶段条件自动推进仍在授权范围内的后续项。

| 工作段 | 任务 | 允许并行的范围 | 阶段出口 |
| --- | --- | --- | --- |
| S0 文档与基线 | D00 → C01 | 文档按互不重叠文件处理；本机状态由一个执行者核 | 文档集成、17处差异与关键交付材料得到保护 |
| S1 修正触发与索引 | C02、C03、C04；C07可只读推进 | 互不写同一文件时并行；C02/C03共享测试文件的改动串行整合 | 文档不误触发全矩阵、24来源正确、旧发布触发方案落实 |
| S2 收束工程产物 | C05 → C06 | 精确目录认领可并行读；迁移、事务、构建和测试写入串行 | 必要证据保留，可重建产物有退出记录，入口可追溯 |
| S3 工程整理交付 | C10 | 只汇总已完成项；C07未完成不伪装纳入 | 工程整理可单独完成，A2继续OPEN |
| S4 生命周期整改 | C07 → C08 → C09 | 合同审查和只读引用核查可并行；同数据根变更串行 | 资源复用、可信计量、历史查询/恢复和退出均承接 |
| S5 事故关闭 | C11 | 复核已存在证据，不重复制造完整测试树 | A1–A6逐项满足才关闭T0 |

C08实施还依赖C03和C05。C10若在C08/C09之前完成，只交付工程整理；后续生命周期实现完成后沿同一交付步骤补做该变更的必要验收，不重复重做未变化部分。

任何时刻只允许一个主体写同一仓库文件、候选事务目录、正式data-root或迁移清单。并行人员/智能体只承担清楚划分的文件或只读核查；主执行者统一集成与验收。遇未知项只暂停该项和依赖，不把无关已明确事项全部卡住。

## 4 任务执行定义

每项以下列唯一方法推进。可变状态仅写同目录consolidation-20261026-tasks.json，本文不复制另一套百分比或完成表。任务登记的planned表示已定义但尚未实施；blocked必须附具体证据、已完成部分和恢复条件。

### D00 集成文档基线与单一任务登记

**问题现象：**当前入口、历史说明和实际代码混杂；方案形成与现场整改容易被记成同一状态。

**根因或待验证原因：**跨轮追加说明未同步当前入口，文档身份和执行结果没有分开。

**唯一指定方法：**在本机先核文档基线和未提交修改，按补丁逐文件集成已准备的文档；保留历史报告原日期、SHA及验收范围。只用本计划定义任务，用同目录任务登记保存可变状态；project-status只摘要当前事实。

**涉及文件、模块或函数：**AGENTS.md；README.md；next/README.md；docs/project-status.md；docs/execution-notes.md；docs/data-authority.md；next/docs/data-authority.md；next/docs/architecture.md；next/docs/core-contracts.md；next/packs/wvd/README.md；next/docs/deprecated-assets.md；docs/work-packages/consolidation-20261026.md；docs/work-packages/consolidation-20261026-tasks.json。

**禁止修改范围：**不覆盖本地不同版本；不改历史报告事实；不修改全局AGENTS、个人SKILL或其它项目；不把待实施规则写成现行生产行为。

**验收断言：**

1. 所有修改路径符合文档允许清单，差异可审阅，相对链接可解析。
2. 现行入口、当前快照、历史记录和拟实施方案各自明确；配置与业务语义未变。
3. 文档补丁已集成与源码整改已完成使用不同状态；仅完成本任务不关闭事故。

**失败后的安全停止条件：**任一文档存在不能自动合并的本地修改时保留原件，逐项合并；如与现有用户决定冲突，暂停冲突项并列明，不选择覆盖。

**依赖与模型：**无前置任务；GPT-6.1 Sol。

### C01 核本机基线并保全唯一材料

**问题现象：**另一工作区有17个未提交路径；当前EXE对应PDB和正式绑定尚未形成可恢复的整理基线。

**根因或待验证原因：**提交祖先关系不能覆盖未提交内容，候选打包未包含PDB。

**唯一指定方法：**读取既有盘点索引并核其SHA；增量复核两工作区状态、当前服务身份和正式配置。保存另一个工作区的HEAD、暂存/未暂存二进制差异及必要未跟踪内容，逐文件给出承接结论；对EXE/PDB复用现有GUID+Age只读检查，再保存配对SHA及恢复位置。保留当前正式候选、已核必要回滚候选、原批次及失败证据。

**涉及文件、模块或函数：**D:/programcode/python/wvd；S:/codex_workspace/codex-home/worktrees/critical-fixes-20261006/wvd；next/.local/consolidation-inventory-20261026/；next/.local/t0-candidate-261025/；next/.local/c11-flow-product/data；next/build/Release/；next/tools/collect_memory_stacks.ps1（仅复用配对检查，不启动整套采集）。

**禁止修改范围：**不reset旧工作区、不stash后丢弃、不复制整套正式data、不按历史PID控制进程、不因busy=false清除源缓存。

**验收断言：**

1. 17个路径均有HEAD/index/worktree差异及承接结论，必要未跟踪内容可恢复。
2. EXE/PDB GUID+Age一致，两文件SHA及原路径已登记；缺符号时保留原build。
3. 41份既有配置快照逐项比较；出现新用户编辑时建立新基线并保留差异，不恢复旧值。
4. 当前服务、候选、端口、data-root及启动绑定来自实时只读核对；监控和循环保持其已有状态。

**失败后的安全停止条件：**身份变化、未知修改、配置哈希变化或符号不匹配只阻断依赖该基线的写操作；不得自动回滚用户新修改。

**依赖与模型：**D00；GPT-6.1 Sol；差异承接争议由 GPT-6 Astra 复核。

### C02 修正文档变化触发全模块回归

**问题现象：**select_modules将next文档变化兜底选成全部模块，整理会无必要地生成新构建和大夹具。

**根因或待验证原因：**回归选择器的文档分类与source_identity现有产品输入分类不一致。

**唯一指定方法：**在select_modules入口按source_identity既有文档定义过滤docs/、next/docs/及README.md、AGENTS.md，再执行原模块映射。沿现有测试文件验证真实git差异输入和混合变更，不使用后缀泛化去跳过manifest、锁文件或数据。选择器本身是工具代码，修改后的必要工具/相关合同验证仍执行一次。

**涉及文件、模块或函数：**next/tools/run_contracts.py::select_modules/changed_paths；next/tools/package_functional.py::source_identity（对齐口径，非顺便重构）；next/tests/test_build_resource_contract.py。

**禁止修改范围：**不改变产品变化的原回归范围，不静默降低CI覆盖，不把manifest或JSON元数据归为文档，不借此跳过选择器自身的验证。

**验收断言：**

1. 仅next/README.md、next/docs/*.md、pack README变化时模块集合为空。
2. 文档与native/storage混合变化仍选择app/devices/runtime。
3. manifest变化与依赖/准备/构建核心工具变化仍触发原要求。
4. 未跟踪和暂存源码变化继续被发现，.gitattributes既有CI处理保持。

**失败后的安全停止条件：**任何真实产品变化被漏选则不得使用新选择器；保留旧选择结果，修复分类后再验证。

**依赖与模型：**C01；GPT-6.1 Sol。

### C03 修24条素材来源并补引用证据

**问题现象：**24条source路径错误，295条source_refs为空或缺失，旧废弃计数不再代表当前引用。

**根因或待验证原因：**历史索引未随扩展作者源迁移，既有生成函数不修正已存在成员行的source。

**唯一指定方法：**只按任务登记中的24条已核映射修改manifest来源；按现有files+aliases身份算法重算revision。重建当前语义、原生、公共流程、动态模板、别名、测试、旧Python和用户头像用途的引用证据，更新原废弃清单。来源恢复用隔离小夹具验证，后续正式构建使用新的产品输入身份。

**涉及文件、模块或函数：**next/packs/wvd/manifest.json；next/tools/package_functional.py::sync_authoring_resources/seed_missing；next/resources/authoring/semantic-assets.json（只读引用核查）；next/docs/deprecated-assets.md；next/tests/test_build_resource_contract.py（与C02写入串行）。

**禁止修改范围：**不改591张PNG字节、成员路径、别名或运行语义；不移动图片来迎合坏索引；不按空source_refs、未入包或重复SHA判废。

**验收断言：**

1. 24条source全部存在，大小与SHA256均与包内成员一致，其余source不变。
2. 593成员、591 PNG、每成员path/hash/bytes及aliases不变，revision按原算法正确重算。
3. 缺失成员恢复不再因这些source报RESOURCE_SOURCE_MISSING；原本字节不符仍拒绝。
4. 295=274空数组+21缺字段、38=17+21当前有效引用得到可复核索引；4张头像原图仍按真实用途判定。

**失败后的安全停止条件：**来源字节不符、多作者来源不明确、需要改变模板名或运行配方时暂停该项；不能通过重生成图像解决。

**依赖与模型：**C01；GPT-6.1 Sol。

### C04 隔离旧Python发布触发并完善原生验证触发

**问题现象：**push master会匹配旧Python发布；原生合同没有push触发，直接快进既可能误发布又不等于已原生验证。

**根因或待验证原因：**旧发布工作流与个人维护主线没有分离触发责任。

**唯一指定方法：**保留旧Python构建与发布步骤，将触发入口改为显式workflow_dispatch；原生合同保留PR/dispatch，并增加个人master相关产品路径push验证。事件映射固定为：PR使用base.sha；push使用event.before且先核非零、合法SHA及该commit在检出历史中存在，不满足则完整合同；workflow_dispatch执行完整合同。不能把空PR上下文当有效增量基线。只调整触发和必要基线选择，不增加原生自动发布。

**涉及文件、模块或函数：**.github/workflows/build-executable.yml；.github/workflows/native-contracts.yml。

**禁止修改范围：**不删除旧src或发布功能、不修改历史Release、不把上游独有79提交并入、不绕过权限、不自动启动workflow或推master作为试验。

**验收断言：**

1. 普通个人master推送不再匹配旧Release工作流；手动入口仍保留原旧产品流程。
2. 原生相关变化在个人仓库选中既有合同；文档过滤与C02一致。
3. PR、push、dispatch三类事件的基线选择可静态核对；实际Actions运行结果单列，未运行不写通过。

**失败后的安全停止条件：**事件/基础SHA/权限不能确定时停止主分支推送，继续不依赖远端发布的本地整理。

**依赖与模型：**C01、C02；GPT-6.1 Sol。

### C05 测试产物收尾及既有大夹具处置

**问题现象：**多轮真实边界测试留下完整65,536记录树，执行结束只有回执落盘，没有明确可重建夹具退出。

**根因或待验证原因：**测试运行和证据交付责任没有覆盖工作区产物生命周期。

**唯一指定方法：**在现有run_contracts.execute收尾中记录本轮所有者、生成路径、预算、实际增量及必要证据；先保存稳定的输入/代码/二进制身份、回执、错误和复现命令，再只处置明确登记的可重建大夹具。成功的真实请求边界保留结果和计量，不长期保留整棵记录树；失败现场先保留完整首发证据，只有最小复现和归因证据成立后才能按精确清单退出重复夹具。旧a2/a3/a4逐项沿同一准则认领，只通过精确处置清单迁移；execute不扫描或清理既有兄弟目录。

**涉及文件、模块或函数：**next/tools/run_contracts.py::execute；next/tests/native/submission_store_contract.hpp（保留真实边界与断言）；next/tests/test_build_resource_contract.py；next/.local/t0-repair-261025/contracts-final-a2；next/.local/t0-repair-261025/contracts-final-a3；next/.local/t0-repair-261025/contracts-final-a4。

**禁止修改范围：**不删除/缩小65,536条真实边界，不修改拒绝65,537条断言，不复用脏output，不用整轮失败作为无限永久留树理由，不把清理失败覆盖原测试错误。

**验收断言：**

1. 边界仍真实写入65,536条并拒绝下一条，重启/幂等/故障断言保持。
2. 每次执行都能报告测试结果与收尾结果；收尾失败保留原错误及准确残留路径。
3. 重复执行前后大夹具净留存不按次数增长，必要结果和复现材料仍可读。
4. 本轮只做预算内必要运行，不为证明收尾再制造多份大树；影响未变的旧证据按身份复用。

**失败后的安全停止条件：**生成物所有者不明确、越出本轮root、遇重解析点或必要证据不完整时不移动该项；清理失败不得伪报收尾完成。

**依赖与模型：**C02；GPT-6.1 Sol。

### C06 候选和构建收束及非正式材料迁移

**问题现象：**默认dist、正式候选、previous/staging及构建物混杂；旧标记使默认打包受阻。

**根因或待验证原因：**目录事务只保证单次替换安全，历史previous成为以后每轮扫描和验证输入。

**唯一指定方法：**先保留C01配对符号和当前/必要回滚候选，逐项认领next/dist历史、重复候选、build和旧隔离产物。用同一处置清单把明确可退出的非正式生成物可恢复迁出当前工作目录；旧格式候选保留原真实身份。更新入口事实；真正更新默认候选继续使用既有package事务及manage_service验证链，与运行服务切换分开执行。

**涉及文件、模块或函数：**next/dist/；next/build/；next/.local/中的已逐项认领候选与隔离生成目录；next/tools/resource_generation.py::directory_transaction（现有机制，不放宽校验）；next/tools/package_functional.py；next/tools/manage_service.ps1。

**禁止修改范围：**不伪造71处旧标记、不提高inventory上限掩盖历史、不改DELIVERY_STATUS为虚假实机通过、不复制service-launch.json伪装部署、不按目录名整批移动。

**验收断言：**

1. 每个迁移项有旧新路径、身份/必要SHA、用途、替代或保留理由及恢复证明。
2. 正式候选/data-root/服务绑定不变；默认入口与文档一致，打包历史准入问题有真实处理结果。
3. 逻辑/唯一内容/实际占用/可回收量分列，同盘trash的磁盘释放记为0或未测，不能写成释放逻辑量。
4. 构建符号、未结诊断、当前依赖与旧用户资产没有误迁。

**失败后的安全停止条件：**出现活动绑定、pending事务、所有者未知、PDB未配对或跨盘完整性不足时保留该项，停止依赖操作。

**依赖与模型：**C01、C03、C05；GPT-6.1 Sol；候选引用争议由 GPT-6 Astra 复核。

### C07 冻结运行资源复用与历史承接合同

**问题现象：**每轮完整发布树累积，单root Bundle与program/bundle revision合同限制了直接共享路径；配额与200轮目标可能不兼容。

**根因或待验证原因：**接管前回滚已实现，接管后的资源复用/引用退出与历史消费尚未闭合。

**唯一指定方法：**沿现有发布入口采用本计划第5节指定的published/.asset-sets资源集合及每请求程序/布局记录；逐一核Bundle/Service/Coordinator/OCR/录帧工具对root、manifest和revision的消费，落实组合租约、身份绑定及旧布局兼容的接口清单。程序身份、业务语义、API旧字段和旧请求防重保持。第5节是唯一拟实施路线，不能只改publisher路径或放松校验；仍有对外语义冲突时明确停在冲突，不自行替换架构。

**涉及文件、模块或函数：**next/native/recognition/request.hpp::Bundle 与 service.cpp::Service；next/native/platform/windows/bundle_lease.hpp/.cpp；next/native/games/wvd/tasks/native_publisher.hpp/.cpp；next/native/runtime/native_run_coordinator.cpp；next/native/app/application.cpp；next/native/storage/run_store.*；next/tests/native/相关发布、租约、录帧及Application合同。

**禁止修改范围：**不新增外部执行器/清理守护器/数据库，不改profile与workflow权威，不以任意外部路径或放松目录闭包实现共享，不静默改变root/revision合同。

**验收断言：**

1. 逐消费点列出旧行为、新物理读取与保持的身份语义；旧包、当前包和历史证据都能定义无歧义读取。
2. 未结输入、源缓存、预览、worker join、batch释放及历史复现引用均有保留条件。
3. 逻辑引用量和唯一保留内容量分开；200目标兼容验收有具体输入和计算，run40不被无证据归因。
4. 本任务结果能直接限定C08文件/接口/测试；若仍需改变既定对外语义，必须明确冲突并停止该分支实现。

**失败后的安全停止条件：**存在无法保持的既有root/revision、历史读取或数据兼容合同，则停在具体冲突处，保留完整设计与反例，不先修改生产布局。

**依赖与模型：**C01；GPT-6 Astra。

### C08 实现运行资源复用与可信计量

**问题现象：**硬链接满后复制仍产生每轮独立模型，非ONNX每轮复制，跨批历史总量不受单批额度约束。

**根因或待验证原因：**目前按请求物化完整资源，未消费共享资源和请求身份的分离合同。

**唯一指定方法：**严格按C07冻结的兼容映射在原发布/租约链内实现：共享对象从锁定源复制并校验一次；每请求保留独立程序、身份与必要引用记录，读取由原BundleLease核验后持有。准备失败仅撤回本次拥有、尚未接管且未被其他消费者引用的对象；已接管共享缓存及既有对象不随该请求失败撤回；终态后按真实join/释放退出活动引用。保留16GiB额度数值、批次内2GiB预留所对应的14GiB续轮门槛，以及独立的卷可用空间2GiB加本次最坏物化成本保护，准入覆盖唯一共享资源集合、请求元数据及runs增量，不能因共享根移出请求子树而漏计；原retained_reference_bytes继续表达路径逻辑引用量，新增内部预算口径显式区分，并在C07写清字段及消费者兼容。

**涉及文件、模块或函数：**C07冻结的必要文件清单；以application.cpp、native_publisher.*、bundle_lease.*及原Service/Coordinator消费点为限；原发布/准备/租约/批次计量合同；next/tools/inventory_publications.ps1。

**禁止修改范围：**不抬高限额、不重置批次逃避上限、不改变业务成功计数、不依赖每轮新硬链接、不删历史账目、不绕过未知输入/坏资源/哈希校验。

**验收断言：**

1. 相同资源清单多轮/多批执行后只保留既定共享内容，新增重型资源字节与链接数不随轮数线性增加。
2. 不同语言、模型、头像和作者版本隔离；资源变更不会污染已冻结运行。
3. 失败/取消/崩溃/共享对象被占用时主错误与清理错误分开，旧引用可恢复。
4. 旧布局和新布局可读，真实16GiB超额及2GiB低空间仍拒绝，logical与retained字段语义明确且消费者一致。
5. 以固定任务配置验证200目标的存储可达性；离线资源循环不等于真实游戏200轮通过。

**失败后的安全停止条件：**任何活动/历史引用损坏、旧布局不能读、数据哈希变化、预算未知却继续写、需降低现有校验才能运行时停止并保留原布局。

**依赖与模型：**C03、C05、C07；GPT-6.1 Sol 实施，GPT-6 Astra 复核引用与恢复。

### C09 正式历史保留与受控归档承接

**问题现象：**真实业务回执持续增长，现有RunStore只按原目录读取；直接移出会破坏历史查询或请求幂等。

**根因或待验证原因：**业务历史与派生资源的退出条件及归档消费者尚未实现。

**唯一指定方法：**先保留requests和runs原权威事实，按实际查询/恢复职责确定必须在线与可归档集合；保留历史以原身份与恢复关系可定位。归档读取继续由原SubmissionStore/RunStore消费并兼容旧目录，原请求ID不得重新变成可执行新意图；完整写入、校验、索引承接和恢复验证后才允许精确迁移原材料。迁移完成后，只有在线与归档权威索引完整且确认ID不存在时才返回未找到；归档缺失、损坏、索引不完整或不可读须报告未知/读取失败并拒绝新执行，不能返回nullopt让旧ID变成新意图。保留期限与容量由已有合同和现场容量测量确定，不在本包猜天数；明确前保持保护和准入拒绝。

**涉及文件、模块或函数：**next/native/storage/submission_store.hpp；next/native/storage/run_store.hpp/.cpp；现有Application/API历史消费点（由引用核对确定）；docs/data-authority.md；next/docs/data-authority.md；精确正式历史迁移清单。

**禁止修改范围：**不只做目录搬家、不丢未知/未结意图、不让归档请求重执行、不把不可访问归档当查询兼容、不全量上传用户配置/日志/转储。

**验收断言：**

1. 历史查询、诊断读取及恢复均能以原身份实际完成，旧ID幂等结果保持。
2. 未结副作用、无终态、必要失败证据和活动源继续受保护。
3. 在线保留、归档集合、预算与退出条件形成明确合同；没有该合同不得将A2/A5/A6写为完成。
4. 迁移前后正式配置及业务结果不改写；中断可恢复、误操作可按精确映射撤回。

**失败后的安全停止条件：**保留规则尚未确定、消费者不兼容、归档不可读、请求防重丢失或恢复证据不足，停止正式历史迁移并保留原件。

**依赖与模型：**C07、C08；GPT-6 Astra 负责保留合同，GPT-6.1 Sol 实施。

### C10 整合验收与个人主线交付

**问题现象：**分支收束、构建、打包、部署和实机验收容易混为一次成功。

**根因或待验证原因：**不同输出身份与验收层级没有统一记录。

**唯一指定方法：**先汇总已完成任务的精确变更和对应证据，未完成C07-C09保留OPEN；复用仍匹配的验证，仅运行变更影响的必要合同。主线整合仅纳入已承接且验证的变更，个人fork跟踪关系明确；提交/推送/部署仅在会话已经明确授权的范围内进行，不通过计划文本自动扩权。源码、构建、候选、部署和实机范围分别写入原状态页。

**涉及文件、模块或函数：**个人codex/t0-lifecycle-repair-20261025分支与master；docs/project-status.md；本任务登记；现有构建/打包/部署入口。

**禁止修改范围：**不合并上游分叉、不强推、不夹带.vscode或未知修改、不捏造Actions/实机通过、不把未实施A2并成已完成。

**验收断言：**

1. 所有纳入路径有负责人、目的、对应验收与剩余限制；工作区差异可解释。
2. 提交和推送身份只在真实执行后登记；当前修复、默认候选和正式部署关系准确。
3. 工程整理里程碑可以单独完成；事故状态仍由C11的A1-A6证据决定。

**失败后的安全停止条件：**远端HEAD变化、未承接修改混入、发布触发未落实、必要验收失败或动作未获相应授权，停止对应外部/现场操作，保留可审阅结果。

**依赖与模型：**D00、C01、C02、C03、C04、C05、C06；GPT-6.1 Sol；跨模块差异由 GPT-6 Astra 复核。

### C11 事故关闭与维护交接

**问题现象：**报告交付和局部通过曾被误解为整体治理完成。

**根因或待验证原因：**缺少按A1-A6逐项关联的关闭证据以及长期维护完成条件。

**唯一指定方法：**按原事故A1-A6逐项核证据，更新原事故报告状态和当前事实；保留所有原始时间线。只有运行资源复用、业务历史承接、旧产物处置、预算与交付职责全部闭合才关闭。建立日常结束记录：新增产物、必要证据、残留、预算、下一任务；只复核本轮增量。

**涉及文件、模块或函数：**docs/reviews/t0-storage-lifecycle-20261026.md；docs/reviews/consolidation-inventory-20261026.md（新增处置结果，不回写旧盘点）；docs/project-status.md；本任务登记。

**禁止修改范围：**不以同盘trash、单轮运行、离线数量、暂时空闲或一次剩余空间充足代替关闭；不消失未解导航/内存等独立问题。

**验收断言：**

1. A1-A6每项都有与本次源码/现场一致的结果和证据，无仅凭计划设为done的项。
2. 必要配置、历史、引用和恢复能力完整；下一阶段维护入口明确。
3. 未知量仍为null或明确未测，所有独立未结问题仍保留。

**失败后的安全停止条件：**任一A项未满足或证据与身份不一致，事故保持OPEN，记录阻断项，已完成工程整理不倒退也不冒充整体验收。

**依赖与模型：**C01、C02、C03、C04、C05、C06、C07、C08、C09、C10；GPT-6 Astra 复核，GPT-6.1 Sol 更新记录。

## 5 A2 单一路线与内部兼容设计

本节是拟实施设计，尚未进入生产代码。C07负责把下述指定布局逐消费点核实并形成最终接口清单；发现与既定对外语义冲突时明确停在该冲突，不让实现者临时另选架构。资源清单保持原完整最终集合，本轮不顺带做按当前场景裁剪。

### 5.1 指定物理布局

| 位置 | 拟承担的职责 | 身份和保护 |
| --- | --- | --- |
| published/.asset-sets/<resource_digest>/manifest.json | 不可变资源集合描述，schema=1；排序后的逻辑path、sha256、bytes | 资源摘要按确定性序列化计算；描述与payload共同受共享侧租约保护 |
| published/.asset-sets/<resource_digest>/payload/ | 原最终资源成员的唯一一套物化字节，保留原逻辑相对路径 | 同清单后续请求直接复用，不再逐轮复制或增加模型硬链接 |
| published/<request_id>/program/flow.json 与同目录identity.json | 每请求程序与原身份材料 | 保持原program identity/revision算法与字节语义，不用资源digest替换 |
| published/<request_id>/publication-layout.json | 新布局显式记录：schema=1、kind=shared_resource_set、resource_digest、program_revision | 仅允许摘要定位上述私有根，不接受任意路径；自身属于请求侧严格元数据清单 |
| 现有requests、runs及必要provenance | 幂等、业务终态、运行关联及原来源证据 | 本阶段保持原位、原权威与原读取语义 |

选择前导点的.asset-sets，是因为既有request_id只允许字母、数字、下划线和连字符；不占用合法用户请求ID，也不修改旧ID的幂等查询行为。若现场已经有该私有路径，必须核所有权和格式，未知时阻断，不能据名字接管或再换UUID绕开。

资源集合摘要材料固定为schema与files数组；files按规范相对path排序，每项包含path、sha256、bytes；JSON键排序、UTF-8、无额外空格、Unicode不转义，再计算SHA256。路径规范沿现有checked_relative规则，不大小写折叠、不接受..、绝对路径或重解析点；同路径重复、大小写冲突及不同hash均按现有平台与manifest规则拒绝。aliases和程序语义继续归原program identity；它们不因共享物理字节而丢失。

### 5.2 身份绑定和组合租约

Bundle.root继续表示请求根，Bundle.revision仍与program.revision一致。现有BundleLease的普通单根构造保持供旧布局使用；在同一实现内部补受信组合构造，由已经完整验证的共享侧租约和请求侧租约建立逻辑成员到持有句柄的映射，不开放任意外部路径。

共享资源集合严格取旧发布器published_manifest=base_manifest并应用所选mod覆盖后的成员，在追加program/flow.json、program/identity.json和parameters/image-sources.json之前固定。这些生成程序/身份/provenance文件属于请求侧；新layout marker属于控制元数据。逻辑联合保持原资源与程序成员，控制文件独立核验，不能因为provenance不在program/下就重复纳入共享集合。

共享侧必须锁住集合根、manifest.json及全部payload成员，不能只锁payload后让描述文件处于可替换状态。请求侧锁住程序、身份、布局标记和既有必要provenance。两侧分别严格核物理闭包，逻辑联合成员必须无重复、无遗漏，保持verify_members、hash、copy/read等验证职责；组合对象持有两侧租约直到全部消费者退出。

仅核“两个目录内部hash都正确”不够。加载时必须按原identity.source_files及image_sources/mod_files既定覆盖规则重建最终资源清单，逐项核对共享manifest；核原identity摘要、program.revision与flow内容一致，再允许组合。资源digest和layout控制元数据不加入原program identity材料，不改变旧程序revision算法。

所有直接拼接Bundle.root/member的资源读取都必须改为经过同一租约确认的物理成员解析。Service::recognize_ocr和OcrEngine的det.onnx、rec.onnx、keys.txt三份输入路径都要由已验证逻辑成员解析，OcrEngine继续持有组合租约直到销毁；不能只改ONNX而漏词典，也不能取出裸路径后提前释放租约。保留模型目录内部接口时，必须证明三份成员同属被固定的目录。普通图片沿已有AssetResolver和lease bytes读取，不改为直接访问共享根。

组合模式须统一承接require_member、hash、member_size、bytes、copy_member、link_member、verify_members、file_count、identity、storage_stats及需要的物理成员解析。copy/link从已验证底层句柄操作，不重新按逻辑根打开文件；逻辑成员数量与物理控制文件数量分列，不污染原诊断统计。两侧同名及Windows大小写折叠冲突均拒绝。

旧默认BundleLease构造、Service在lease缺失时的装配、Publisher的baseline/mod源、历史录帧重建统一调用同一受信布局读取入口，在现有BundleLease/Publisher实现内复用，不分散实现多套marker解析。新布局标记存在则严格按新格式读；未知、损坏或不一致标记直接失败，不猜测性回退。无标记的原完整树仍走严格旧布局读取。禁止只改Publisher，再放宽Service的root/revision一致性断言。

### 5.3 事务、引用和退出

首次物化沿既有锁定源复制、staging、完整校验、同盘rename；已有集合只有身份及全部成员一致才复用。共享集合与每请求目录分别记录清理所有权；失败仅撤回本次尚未接管的对象，不能把其他请求仍使用的共享集合随本次回滚删除。已验证共享缓存可保留，但必须纳入持久保留计量，不能成为漏计目录。

现有require_no_pending只扫描一层。Application准备门、首次物化、所有复用命中路径及重启后的首次准备，都要检查published/.asset-sets及本次相关事务层，覆盖内部staging与cleanup-pending；仅在创建分支检查会被复用路径绕过。原published及asset-objects保护继续保持。共享集合生成后marker尚未写、marker已写但尚未接管、准备失败未清理及进程中断的窗口由原准备所有权、事务证据和活动租约保护；没有正式marker不能解释为零引用。

活动引用通过现有NativeUnit/NativeRunDefinition、准备对象与Session持有，不新增持久全局refcount作为第二权威。磁盘引用从不可变请求布局、runs/run.json、result、repeat/handoff关系及必要恢复材料核对。来源缓存、预览、准备、worker、批次冻结源仍持有时保留；result_saved、quiescent、busy=false任一字段都不能单独证明全部退出。worker_definition_released、worker_joined、batch_payloads_released等原边界分别记录。

释放活动引用仅意味着可以进入保留判断。历史还需要恢复的共享集合保持一份；未知历史继续保护。旧完整发布树转换须先证明原程序/资源按原SHA可恢复并由消费者承接，再按精确计划执行，不从当前作者源重建旧历史。

inventory_publications.ps1当前会把published每个直接目录当普通请求统计，恢复旧盘点的ID规则也不接纳前导点。它必须显式区分请求、私有资源集合、模型对象、staging/事务及未知目录，并同步盘点恢复格式，分别报告引用；不能把共享根“同名request引用0”当作孤儿。工具本来声明活跃租约未知，仍不得通过静态盘点给自己新增回收权限。

### 5.4 容量计量和历史边界

两个独立条件均保持：批次计量后的保留量不超过14GiB才允许下一轮，保留16GiB硬上限及批次内2GiB预留；卷可用空间另外满足2GiB余量加本次最坏新增物化/事务成本。不能只检查累计未到16GiB且卷尚余2GiB。新准入必须覆盖：该批次要求保留的唯一共享集合、独立请求元数据和runs/诊断增量。共享根不在请求子树内，必须显式纳入，不能因扫描目录变小漏计。下一轮开始前核新增物化成本；首次集合、不同模型/头像/语言版本分别计算，现有集合按真实身份计一次。

原retained_reference_bytes继续表示路径逻辑引用口径，不能改同名字段为实占、置零或隐去增长。新的内部保留预算沿现有状态/诊断结构明确命名和说明，C07列全API/前端/工具消费点后实施；逻辑量、FileID内容量、簇实占和可回收量保持区别。相同SHA但不同文件对象仍占独立内容，不按hash误扣。

200次离线发布用一套小型真实资源夹具验证资源集合、链接数和大字节留存稳定；真实任务清单/模型尺寸用于预算复核；必要真实模型/链接边界只在依赖变化时做一次定向验收。离线200次发布不计入游戏成功轮数，不自动恢复39/200批次。如果必要业务诊断本身超过原预算，记录具体冲突并停止，不自行降低证据或提高额度。

请求和终态事实仍会小量增长。C09必须明确在线保留、归档、查询、恢复与防重执行的消费者承接，才能声称总体长期维护闭合；本包不猜保留7天、30天或删除旧请求数量。C09未完成时，资源复用可以验收，事故仍不能整体关闭。

## 6 产物预算与精确迁移规则

所有本轮小型执行证据集中到一个已核所有权的短根，例如next/.local/c26-consolidation/。每次合同执行使用其下全新子目录，避免脏夹具复用；不得为每次失败再建立一套无归属完整工作树。2026-10-26原盘点证据保持只读，新的处置回执不覆盖原扫描。

大规模构建、真实大边界、候选复制或迁移前，执行者从现有清单与所选case填写：输出根、预计新增文件数、预计逻辑字节、首次物化/复制最坏量、真实卷可用量、必要证据及结束去向。数字从输入推导，不凭“还有很多空间”填写；未知则该大写入尚不具备执行条件，先做有界只读核算。无需因此重新向用户索取同一动作授权。

处置清单按明确路径逐项登记以下字段：

| 字段 | 必须记录的含义 |
| --- | --- |
| original_path / destination_path | 精确原路径与恢复/迁移位置；不使用通配符充当处置范围 |
| kind / owner / provenance | 用户权威、业务历史、派生资源、候选、夹具、依赖或诊断，以及实际生成者 |
| identity / hashes | 必要文件身份与内容SHA，原始证据索引；避免无目的反复全盘哈希 |
| references / references_complete | 请求、run、内部续轮/交接、活动租约、源码/测试、恢复材料等引用及核查是否完整 |
| terminal / side_effects / transaction | 终态、未结副作用、pending/staging/cleanup状态 |
| decision / reason | 保留、恢复性迁移或待核；明确依据及替代项 |
| before / after / restore_check | 文件数、逻辑字节、可得的其他计量、恢复核验及配置/历史核对 |
| authorization_reference | 当前会话中已经覆盖该操作的授权范围；计划条文本身不新增权限 |

同盘trash改变工作目录位置，不释放容量；不通过自动永久清空来补足“释放空间”指标。跨盘迁移在目标容量、硬链接关系、重解析点、字节完整性和恢复关系核清后才进入执行。未知项保留，不把扫描不完整或FileID=0解释成无引用。根src/config/mod/dist、活动/回滚候选、requests/runs及必要故障证据不进入泛化清理规则。

## 7 项目管控与交付纪律

### 7.1 一个状态来源

本计划保存范围、方法和验收定义；任务登记保存状态、结果、证据、执行提交和阻断。project-status只摘要当前入口和未结项。事故报告保存原始事实与关闭证据，execution-notes保存执行限制及已验证经验。文档不各自复制一份可变任务表。

任务状态限定为planned、in_progress、verification、done、blocked。只有验收断言和证据满足才为done；“代码已写”“报告已交”“已推送”都不能直接代替。blocked需写具体原因、已完成范围、恢复条件及依赖，解除后回到真实阶段。artifact_prepared仅表示交付包已有内容，与本机集成状态不同。

每个会话开始读project-status和一个正在推进的任务；结束回填实际改动、验证、产物去向、剩余条件和下一项。正常巡检按新增事件/回执游标，不重读全部历史，不默认取图，不把本地证据保留等同全部送入模型。

### 7.2 变更和分支

文档修订、回归选择器、素材metadata、CI触发、测试收尾、运行生命周期按目的分开提交，避免单一巨大混合提交。只有同一兼容变更必须原子落地时合并必要文件。当前修复分支是收束基线；origin为上游、fork为个人，推送必须明确远端和分支。未知本地修改、.vscode、正式配置、模型、日志和dump不夹带进提交。

本包只准备文档补丁和推进材料，没有提交、推送、创建PR、触发Actions或操作master。后续外部动作沿用户已授权范围执行；未获相应授权时保留已完成可审阅结果，说明具体动作，而非重问整个计划。自动审批拒绝时明确记录动作及原因，不改规则绕过。

远端HEAD或本地输入变化时只复核影响到的文件/合同；不得强制覆盖、重写历史或擅自合并上游分叉。报告基线205692c保持为历史，179cfd8是此次审阅提交，两者不回写成以后HEAD。

### 7.3 验证和证据复用

复用旧验证先核产品输入、相关二进制、依赖及测试前提一致；只改文档不要求重建产品。修改manifest来源会改变产品输入，即使PNG不变，也不能拿旧build receipt宣称新候选已验证。真正受影响的完整逻辑链仍验证：输入来源→存储→处理→接口→消费→验收。

采用定向合同及最小真实夹具，保留原核心断言。选择器修改本身要验证选择行为；共享资源改动要验证取消、损坏、未知、租约、旧格式、重启及计量。完整65,536边界在依赖实现变化时真实验证一次，不用假的计数替代，也不因文档变化重复制造。

以下五种事实分别登记：源码已集成、构建完成、候选验证、正式部署、实机验收。候选标记BUILT_NOT_GAME_ACCEPTED保持既有语义，不能为表示局部实机通过擅改标记；实机范围由独立证据记录。

### 7.4 问题升级与停止

优先处理用户明确指出的存储失控和工程收束。新发现且不在本任务的业务缺陷只登记证据，不借机改流程。安全停止聚焦受影响项：源/目标身份不符、用户新修改、未结副作用、数据兼容断裂、预算未知、恢复失败或必须改变既定接口语义。停止后保留原件及双错误，不自动扩大重试、全量测试或永久删除。

只读核查、确定性文档合并、已有授权的定向执行可连续推进。管控依靠范围、身份、验收与可恢复性，不增设多层“再次确认”页面或每步人工审批。

## 8 文档补全范围与维护归属

| 文档 | 本包已准备的补全 | 今后更新责任 |
| --- | --- | --- |
| AGENTS.md | 18条项目边界、任务入口、输出路径、预算与证据复用、授权延续 | 项目通用执行规则变化时更新；不放临时PID和项目外规则 |
| README.md / next/README.md | Next与旧Python边界、默认安装与正式快照、验收范围 | 产品入口变化时更新；运行快照链接到project-status |
| docs/project-status.md | 计划/任务登记入口、盘点时点、原历史账目保留 | 每次真实部署/批次/阶段变化后简要更新 |
| docs/execution-notes.md | 文档选择器待修、stage旧行为与显式生成事务区分 | 已验证经验与实际执行限制；旧经验失效时标历史 |
| 两份data-authority | 根入口和详细字段对齐，现行保留与拟整改分开 | 数据责任/消费者真实改变时同步，不能先写成已实现 |
| next/docs/architecture.md | 当前事实入口、两条悬赏repeat准入、2GiB准入、recent队列4、验收范围措辞 | 架构职责变化有源码证据时更新 |
| next/docs/core-contracts.md | 本计划与未实施合同的边界 | 只有实现与验收同步完成后更新现行合同 |
| next/packs/wvd/README.md | 当前593/591、作者源/生成/发布职责、待修索引 | 包成员或职责变化时更新，数字注明基线 |
| next/docs/deprecated-assets.md | 295空/缺、38活跃、24来源、4头像待查；旧表保留 | 引用闭包实测变化后更新，不以空索引判废 |
| 本计划与任务登记 | 方法、依赖、验收、模型及初始状态 | 方法变更需有理由；状态每次执行后更新 |

不批量改写268份历史Markdown，不将112处历史链接候选当作当前产品故障。当前导航用到的链接优先保证；必要历史路径缺失记录原位置与对应证据，不伪造新文件替代原证据。docs/handoff-20261021.md保留原文，不把旧授权改成当前许可。

## 9 里程碑和事故关闭映射

| 原事故项 | 对应任务 | 可宣布完成的条件 |
| --- | --- | --- |
| A1 所有者/引用/未提交保护 | C01、C06、C09 | 精确清单、17处承接、必要SHA和恢复验证全部成立 |
| A2 运行资源与历史生命周期 | C07、C08、C09 | 共享复用、活动退出、历史查询/恢复、防重与预算兼容同时成立 |
| A3 测试产物收尾 | C02、C05 | 原断言保留，必要证据可用，大夹具净留存受控 |
| A4 候选及发布入口 | C01、C04、C06、C10 | 当前/回滚与符号正确，默认/正式关系清楚，旧发布触发已处理 |
| A5 已确认废弃项迁移 | C06、C09 | 精确迁移与恢复可核，正式数据及业务历史完整，空间口径真实 |
| A6 持续预算与完成条件 | D00、C02、C05、C08、C09 | 每类材料有职责、预算、必要保留和退出条件，并有执行证据 |

工程整理里程碑要求D00、C01–C06及C10完成；C07–C09可以仍为未结，明确记录即可。整个T0关闭要求C11核A1–A6全部满足；不以搬到同盘trash、一次磁盘充足、离线数量或一轮游戏替代。

建议日常任务分工：GPT-6.1 Sol执行已冻结的文档、元数据、脚本和定向代码变更；GPT-6 Astra审查资源身份、历史承接、并发/崩溃边界和事故关闭。简单计数、哈希和链接核对由确定性工具完成，不依赖高规格模型逐文件阅读。该分工是任务建议，不是性能或费用实测结论。

## 10 固定提交证据

所有源码引用固定为179cfd851877cba2f2df401d86a905ff9ae088f6，防止阅读过程中main漂移。现场事实以盘点日期为界，执行结果另记。

- [本次提交](https://github.com/livehtworks/wvd/commit/179cfd851877cba2f2df401d86a905ff9ae088f6)：6个文档/摘要路径。
- [T0生命周期事故](../reviews/t0-storage-lifecycle-20261026.md)、[盘点报告](../reviews/consolidation-inventory-20261026.md)、[小型事实摘要](../reviews/evidence/storage-t0-20261026-summary.json)：规模、状态、A1–A6及原证据SHA。
- [Application请求/续轮/计量](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/native/app/application.cpp#L972-L1012)：queue_run；同文件1828–1926为批次16GiB/续轮预留及prepare_task链。
- [Publisher完整清单与发布](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/native/games/wvd/tasks/native_publisher.cpp#L125-L248)：身份、逻辑容量及复制/硬链接行为。
- [Bundle定义](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/native/recognition/request.hpp#L15-L26)、[Service租约门](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/native/recognition/service.cpp#L61-L72)、[OCR物理路径](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/native/recognition/service.cpp#L325-L334)。
- [运行历史读取](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/native/storage/run_store.cpp#L905-L939)、[现有发布盘点工具](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/tools/inventory_publications.ps1#L90-L138)。
- [文档与产品输入](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/tools/package_functional.py#L70-L140)、[模块选择器](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/tools/run_contracts.py#L147-L167)。
- [目录事务历史保留](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/tools/resource_generation.py#L251-L322)、[真实请求边界](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/tests/native/submission_store_contract.hpp#L128-L152)。
- [旧发布工作流](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/.github/workflows/build-executable.yml)、[原生合同工作流](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/.github/workflows/native-contracts.yml)、[PDB配对](https://github.com/livehtworks/wvd/blob/179cfd851877cba2f2df401d86a905ff9ae088f6/next/tools/collect_memory_stacks.ps1#L253-L258)。

## 11 下一次执行从哪里开始

执行者先完成D00的本地文档差异核对与集成，再进行C01。当前产物包没有确定任何现场目录为可直接删除；24条素材来源修正只是已核映射，manifest尚未修改。代码整改、目录迁移、master推送、正式服务切换和游戏续跑按各自已获授权的范围推进；缺现场证据先补精确核查，不退回重复全仓扫描或泛化清理。

每个任务交付统一写清：实际修改、原因、验收结果、证据、产物净增量/去向、仍未验证事项以及下一依赖。完成工程整理后可以继续日常维护；T0仍按上表独立关闭。
