# 维护阶段收束盘点

日期：2026-10-26，Asia/Shanghai。对象：`D:/programcode/python/wvd`、同仓库另一工作区及个人远端 `livehtworks/wvd`。盘点阶段按“先清点”执行：刷新远端引用、只读核现场/目录/资源，新增本报告并更正当前状态；盘点时未合并、提交、推送、删除、移入trash、构建、部署或操作游戏。用户随后明确要求将本盘点及新T0事故报告commit/push，本次只提交报告、事实摘要及相关当前文档，不包含清理、合并或部署。监控未恢复。

后续已按用户授权开始执行工作包，结果见[收束执行报告](consolidation-execution-20261026.md)和[任务登记](../../history/consolidation-state.json)。以下数量、入口缺口和“尚未”结论均为原盘点时点，不覆盖后续已核迁移或CI/素材整改，也不将旧数量冒充当前目录数量。

## 盘点时结论

可以以当前T0修复分支作为源码收束基线，但尚未达到干净、可维护的交付状态：另一个工作区尚有未提交修改；推送master会触发旧Python公开发布；发布历史与测试产物已失控累积，必须按引用和用途完成处置，不能笼统解释为“证据必须保留”。该问题已单列[T0事故报告](../incidents/t0-storage-lifecycle-20261026.md)，整改未完成。资产字节目前一致，来源索引、废弃索引和说明文档仍待更新。

本次证据目录：`next/.local/consolidation-inventory-20261026/`。正式数据未写入；profile及workflows共41份文件与2026-10-25发布前SHA快照一致。

## 分支与远端

已实际fetch个人及上游远端，并用ls-remote核个人远端分支；盘点时无stash、无开放PR。**以下提交、差额和分支表均固定在盘点基线**`205692c7bdea73634e780f1af0c70eef4fa62d0c`，当时与对应个人远端分支一致，后续报告提交不回写该历史基线。盘点开始时主工作区仅`.vscode/`未跟踪；本次用户授权的后续提交范围包括本文、事故报告、事实摘要及相关当前文档。

| 引用 | 当前提交/关系 | 收束判断 |
| --- | --- | --- |
| 当前 `codex/t0-lifecycle-repair-20261025` | `205692c`，个人远端同提交 | 当前完整源码与文档基线 |
| 本地 `master` | `d2d97d2`，是当前分支祖先，落后235提交 | 可快进至维护基线；目前仍跟踪origin/master，收束时应明确个人远端跟踪关系 |
| 个人 `fork/master` | `324c36e`，是当前分支祖先，落后244提交 | 技术上可快进，推送前先处理下面的旧发布触发 |
| 本地/个人 `agent/local-stability-notes` | `01e5042`，被当前分支完整包含，落后3提交 | 收束后可作为已合入分支处置；本轮保留 |
| 个人 `gh-pages` | `809e674`；个人仓库has_pages=false | 历史发布元数据分支，不是产品主线，不能按未合入功能分支判断 |
| 上游 `origin/master` | `d9bca81`，上游独有79提交、当前分支独有213提交 | 已分叉；上游同步不应混同个人分支收束 |

与上游共同基线为`1c37b76783f809beb2457a3fcce75b318284e0b4`。两侧分别改动729/1834个路径，有9个重叠路径：`.gitignore`、`CHANGES_LOG.md`、`README.md`、`resources/quest/quest.json`、`src/{gui,main,script,utils}.py`及旧打包脚本。这是重叠清点，不是合并冲突或功能承接已完成的证明；没有试合并或改写历史。

**主分支发布行为需要先定案。** `.github/workflows/build-executable.yml`在push master时打包旧`src/main.py`，创建GitHub Release并更新gh-pages元数据。原生`native-contracts.yml`目前由PR或手动触发，不能认为直接快进push会自动完成原生验收。收束方案需明确旧发布能力的保留入口及新默认行为，不能在推送时意外发布旧产品，也不能借清理擅自退役现有入口。

## 另一个工作区

路径：`S:/codex_workspace/codex-home/worktrees/critical-fixes-20261006/wvd`，detached HEAD `c2b5bee`，该提交已是当前分支祖先；**工作区内容仍有17个未提交路径**，其中15个涉及暂存、4个涉及未暂存，两类有重叠。一项OCR文本还有换行差异。不能因为基点已合入就认定这些修改已被承接。

涉及原生操作/Boot恢复、BundleLease/FileDigest、Coordinator、RunStore及测试；具体状态在`other-worktree.json`。本轮未修改、暂存、清理或归档该工作区。后续应先保存可恢复的暂存/未暂存差异，再逐项核对当前实现与测试是否已经等价承接。

该工作区另有约3.64 GiB逻辑产物：build约2.00 GiB、`.local`约1.53 GiB、node_modules约0.10 GiB，前端dist不足1 MiB。只测量了这些生成目录，没有将其认定为全可丢弃。

## 正式现场与批次

- 服务17654仍运行：实例`9F2AF8E0-F5BB-4EBC-BAF1-2DA16D6D7653`、PID47024、创建标识`134374039039026061`；现场EXE路径、命令行与service API一致。历史PID不作为后续操作依据。
- 正式候选`next/.local/t0-candidate-261025/`，EXE SHA256 `284e860b078f9e91599ac00f083b21d322359311c5118a1814ef9a0be1c3de34`。正式data-root仍为`next/.local/c11-flow-product/data`。
- 新200目标根`giant-200-t0-20261025-b0ad41332a754a33b4d900f14f3b2424`：本次逐份核run1–39均为Completed、3业务单元、result_saved/quiescent；run40为UserStopped、1业务单元、已保存且静止，排除。内部repeat.active=false、busy=false，累计39/200，剩余161仅是账面差额，未提交任何续跑请求。
- `wvd`监控PAUSED，其保存的提示仍指向更早20261022批次；以后恢复监控必须先重新核对目标，不能直接沿用旧提示。
- 40份磁盘回执路径及SHA在`live-and-receipts.json`；run40回执SHA为`33915f1562bda1fb4aacb85341d4df45c05935a2260892ccf205d21cc9e8ee76`。不从UserStopped反推是谁停止，也不将停止抵消为技术故障续跑。

## 目录与占用

主工作区扫描于19:32–19:45完成：1,911,793个文件、155,145个目录，逻辑大小292.644 GiB；读取错误0，48个重解析点未跟随，均在旧隔离测试目录内。本轮盘点输出目录排除于主扫描。D盘当时可用约128.93 GiB。

**以下均为逻辑大小，不是独占磁盘占用或可释放量。** Windows DirEntry缓存的FileID/链接数字段实测为0；初始去重估计已弃用，`filesystem.json`相关字段置空，探针为`metadata-quality-probe.json`，原始扫描保留并标记在`filesystem-raw-direntry.json`。本轮没有逐个打开191万个文件核物理分配。NTFS硬链接共享、压缩及同盘trash迁移都影响实际回收量；表中父子项不可相加。

补核已经明确大文件数的来源：正式published有1,334份请求快照（1,150个repeat命名目录、184个其他请求目录），抽查一份600文件、约136.31 MiB逻辑大小；最近T0三个合同目录`contracts-final-a2/a3/a4`分别72,045/72,046/71,841文件，合计215,932文件。每轮资源树与重复大夹具未完成收尾，是本次事故的直接表现，不能继续按当前方式无限保留。

19:55对正式published中ONNX以真实`os.stat`核FileID：4,397个路径合计84.152 GiB逻辑大小，2,272个不同文件对象合计33.846 GiB内容字节，路径重复计量差50.306 GiB。这仅覆盖模型文件，不是全仓库实占或可回收空间；不承诺可释放292 GiB。证据为`published-layout.json`、`model-space-proof.json`和`t0-file-counts.json`，小型[可提交摘要](../evidence/storage-t0-20261026-summary.json)已单独保存。

| 路径 | 文件数 | 逻辑GiB | 当前判断 |
| --- | ---: | ---: | --- |
| `next/.local/` | 1,781,632 | 270.68 | 含正式数据、证据、依赖和测试，不可整目录清理；直接子目录903个（含本次盘点） |
| `next/.local/c11-flow-product/` | 923,634 | 180.46 | 正式data与138个candidate命名目录混放；应拆分规划，不能按名字整批移动 |
| 上项中的`data/` | 825,589 | 159.57 | 正式持久化，保护 |
| `data/published/` | 789,906 | 147.38 | 冻结发布和共享模型、历史引用，不能当测试垃圾 |
| `data/runs/` | 8,063 | 8.67 | 正式终态、诊断、输入等运行历史 |
| `data/asset-cache/` | 27,006 | 3.39 | 受租约/历史引用约束的派生缓存 |
| `next/.local/m2-runs/` | 52,175 | 21.72 | 旧阶段运行材料；先查生成者、引用及保留目的 |
| `next/dist/` | 103,569 | 16.23 | 历史发布与残留；优先制定迁移清单 |
| `next/.local/t0-repair-261025/` | 231,730 | 5.23 | 最近T0验收证据与大量隔离夹具；先冻结必要证据索引再区分可重建产物 |
| `next/build/` | 1,227 | 4.86 | 生成物为主，但先保全当前正式EXE对应符号及发布回执 |
| `next/.local/memory-monitor-heap-two-…/` | 64 | 2.43 | 旧失败内存调查，含2.07 GiB不完整CSV；不是完整归因报告，是否保留需结合未结问题 |
| `next/.local/native-deps/` | 2,325 | 2.14 | 当前锁定依赖缓存，不能和废弃Maa依赖混为一类 |
| `next/.local/linkage-aa0957a-20261006/` | 2,239 | 1.30 | 历史隔离构建与验证，按证据/重建物拆分 |
| 根`.venv-build/` | 10,802 | 0.36 | 旧Python构建环境；处理须同时考虑旧构建入口 |
| 根`dist/` | 1,492 | 0.18 | 旧产品/用户资产，项目约束要求保留 |

`next/dist`直接目录121个：105个previous、1个staging、1个事务目录、14个其他目录。106个previous/staging中71个不具备当前交付标记头；其余“头有效”也不等于已完成全量候选校验。默认打包目标仍会被这些历史阻止，不能补造标记或放宽验证。

默认`next/dist/wvd-next-native`标记仍为旧提交`8cdb464`；当前正式T0候选位于`.local/t0-candidate-261025`。因此默认说明/快捷入口与正式部署没有统一，应在收束时核验后统一，不能直接认为默认目录就是最新版。

主工作区共找到435个`automationd.exe`路径；这是文件副本数量，不是运行进程数。唯一查询到的automationd进程为上述正式服务。75个按名称匹配的事务/清理记录中，74处属于T0故障夹具，另1处是`c11-flow-candidate/backup-20260926T120409825125Z/transaction.json`的旧修改备份，不等于75笔生产未完成事务。正式data-root内未命中本次检索的`.cleanup-pending-*`或`transaction.json`文件名。

**构建符号先保留。** 当前候选未附带PDB；`next/build/Release/automationd.pdb`为295,473,152字节，旁边EXE的SHA与正式候选一致。来源和SHA记录在`build-symbols.json`；删除生成目录前应将这组构建产物及对应回执保留到版本归档。不要只因build可重建而丢失这次二进制的诊断材料。

## 素材与资产索引

只读调用现有`check_authoring_assets()`通过；包清单593成员逐项SHA/大小一致，其中591张PNG。148个语义资源使用176个不同模板名，均存在对应图片；两份作者JSON与包内副本相同。没有生成、同步或重新打包素材。

| 项目 | 清点结果 | 后续工作 |
| --- | --- | --- |
| 根`resources/images` | 437文件，约11.36 MiB | 旧来源素材，不能因Next使用包内副本而直接删 |
| `next/resources/images` | 158文件，约4.14 MiB | 当前扩展作者源；保留来源与配方关系 |
| `next/packs/wvd/image` | 591文件，约15.47 MiB | 清单管理的打包输入，重复副本承担真实职责 |
| 清单source路径 | 24条指向不存在的根`resources/images/…` | 实际文件均在`next/resources/images/…`、已跟踪、SHA与包内一致；应修来源索引，不是补造素材 |
| 旧`source_refs`为空 | 295张，其中38张仍被当前语义配方直接引用 | 旧静态索引失效，不能当删除清单 |
| Next源中未入包的图片 | 4张monster-portraits原图 | `cyclops`、`dark_enemy_candidate`、`red_hair_serpent`、`scorpion`；须核用户头像/嵌入配置/来源用途，暂为待确认 |
| 旧废弃清单 | 仍按508张、211待确认的旧快照说明 | 应重新建立当前引用与替代关系，不能沿用旧计数 |

24条来源纠正候选见`source-path-corrections.json`，295条旧空索引及38个有效反例见`asset-reference-audit.json`/`assets.json`。已有字节相同的567组来源均一致，24组经实际Next来源也一致；没有发现源/包的字节冲突。

旧清单的四张`inn_icon/guild_icon/edge_of_town_icon/ruins_icon`可继续作为“已有形状模板替代、待核退出闭包”的候选，尚未证明可整体移出manifest、测试及旧工程。`supportSkillCheck.png`尤其不能直接判废：当前Next不再使用旧识别，但旧`src/script.py`仍有真实调用，Next测试也验证它未进入新战斗图。

目前没有任何图片被仅凭文件名、重复SHA或空引用认定为可立即移入trash。资产体积远小于正式历史和构建，先修权威/引用索引更有价值。

## 文档收束清单

已扫描268份Git跟踪Markdown；112处本地链接缺失候选主要集中在归档/旧阶段报告，包含历史路径，不直接等同当前功能损坏；列表见`document-audit.json`。当前状态、数据职责及交接入口的所检Markdown链接未发现缺失。

| 文档 | 已确认的问题/处理范围 |
| --- | --- |
| `docs/project-status.md` | 本轮已将旧1/200、run2 Running更正为39/200、run40 UserStopped；下一步再精简重复历史，链接指向专项报告 |
| 根`README.md`、`next/README.md` | 根说明以旧Python产品为主体；Next说明仍写默认候选/默认data路径及“游戏未经新后端实机验收”。应明确当前产品入口、正式实例与旧产品说明边界，保留实际验收范围 |
| `next/packs/wvd/README.md` | 仍是M3/M4、437张和旧prepare_m3入口的阶段说明；需替换为当前591张及来源/生成/发布职责 |
| `next/docs/deprecated-assets.md` | 计数和引用索引陈旧，需重算、注明静态/动态/旧工程与替代关系 |
| `docs/execution-notes.md` | 仍有旧条目称stage会同步回写作者副本；当前T0代码已改为显式资源生成事务，须合并当前结论并归档旧现象 |
| `AGENTS.md` | Release路径写作build/native/Release；当前automationd实际位于build/Release，需按目标区分，不能把库/测试目录混作产品输出 |
| 两份data-authority与交接资料 | 根简表与Next详细表应明确主从引用、合并冲突的旧生命周期描述；交接历史保留日期，不继续将旧阶段事实当默认现场 |

本轮没有批量改写历史报告、删除旧测试或修补112处历史链接，也没有把尚待收束的文档宣称为已更新完成。

## 后续执行顺序与trash规则

1. 先保存另一个工作区暂存/未暂存差异及原HEAD，再逐项核真实承接；正式profile/workflows、批次账本、原失败回执和当前/回滚候选单独列保护清单。
2. 以当前已发布修复为源码基线，明确master发布行为和个人跟踪关系；准备保留功能的具体合并改动后再合分支、推送个人远端。上游同步单列，不默认将79个上游独有提交并入此次收束。
3. 固定当前EXE/PDB、交付清单、配置快照和关键离线证据；按目录逐项认领旧候选、测试材料、依赖与历史诊断，再生成精确迁移清单。优先考虑默认dist历史、重复候选、可重建构建/夹具；正式published/requests/runs需另有历史保留及恢复方案，不能混作垃圾。
4. 修正24条来源索引并更新当前引用闭包、废弃清单和入口说明。素材移出有效清单前，必须核原生源码、作者配方、流程、测试、动态名称、旧入口和用户来源。不能用删测试或放宽manifest校验来完成清理。
5. 对确证废弃项使用trash保留原相对路径、SHA、原用途、替代项/退役依据、迁移时间及恢复位置；大体积二进制/运行材料不直接提交Git。迁移后核入口、引用、正式数据SHA及已保存业务历史，记录仍保留的未结问题，再转入日常维护/优化/功能新增。

同盘trash只改变位置，不减少磁盘占用；跨盘迁移需另核空间、硬链接、重解析点、字节完整性及恢复关系。Git中的历史源码、旧用户资产和未提交内容均不能因为“归档”“垃圾”名称被绕过保护。未完成所有权和引用核对的目录继续标为待确认，本报告不授权永久删除。

### 证据索引

- 分支、远端、另一工作区：`git.json`、`remote-summary.json`、`other-worktree.json`、`other-worktree-sizes.json`。
- 容量、扫描边界、符号：`filesystem.json`、`metadata-quality-probe.json`、`published-layout.json`、`model-space-proof.json`、`t0-file-counts.json`、`dist-headers.json`、`build-symbols.json`。
- 正式现场及41份配置：`live-and-receipts.json`、`configuration-comparison.json`。
- 资产与文档：`asset-reference-audit.json`、`source-path-corrections.json`、`source-images-outside-pack.json`、`assets.json`、`document-audit.json`。
