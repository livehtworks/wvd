# T0与同类生命周期缺口修复验收

## 范围与基线

- 最初授权：按`wvd-t0-audit-01e5042.zip`修复、离线验收和交付审阅，不含自动部署/恢复循环/提交。该阶段证据保留如下；用户随后另行明确授权提交、推送、构建打包部署，并确认新的200轮从0开始，见文末发布记录。监控未获恢复指令，保持暂停。
- 基线：`01e5042e34b8a9fb66039450bda8c6335bff5ad6`；修复分支：`codex/t0-lifecycle-repair-20261025`。ZIP的520项源码Git对象逐项核对一致，见`next/.local/t0-repair-261025/baseline/package-source-check.json`。
- 用户原有三份文档修改与`.vscode`已在`baseline/`保全；正式配置40项哈希、正式服务状态也已保存。测试都使用全新`next/.local/t0-repair-261025/`或合同运行器明确创建的可丢弃目录。
- 唯一执行链仍为Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。没有外部循环、自动重启守护器、GC或游戏业务调整。

## WP-01：发布容量及连续所有权

原因为发布持续给同一不可变模型增加硬链接，又没有处理系统真实容量拒绝；同时发布返回后的组装异常与明确接管之间存在目录所有权缺口。

- `bundle_lease.hpp/.cpp`增加携带原始uint32 NTSTATUS的`BundleLinkError`，原错误文本保持。`link_member`复用现有`ancestors`，不放松源锁、目标祖先、reparse和不覆盖规则。
- 统一回归实际触达深目录+哈希临时名超过260字符的失败：所有者创建/扫描/改名/撤回、Publisher目录/空间操作、流式copy目标及摘要读取在系统调用边界使用扩展路径。BundleLease按扩展路径遍历，再映射回原公开root下的相对成员；不改变Bundle/lease/receipt公开路径表示或修改系统长路径设置。
- `native_publisher.hpp/.cpp`只在`STATUS_TOO_MANY_LINKS`时从同一冻结租约以原64KiB缓冲复制；其它错误继续失败。预先按逻辑别名分别计算最坏复制量，缺失内容对象按哈希计一次，加入生成文件/事务余量及2GiB运行保留；未知及uint溢出在staging前拒绝，不加载模型bytes或额外整文件哈希做预算。
- 同文件中受限的`OwnedBundleDirectory`承担独占创建、身份确认、同父目录不覆盖rename和回滚。唯一token从NativePublication传入NativeRunDefinition，再由Coordinator明确接管。接管前先放掉本次租约/units才清理；接管后worker即使很快完成或返回抛错也保留包。
- 最终rename前先独占创建并flush外部准备sidecar，保存源/目标及目录身份，通过同一打开句柄持有；明确接管或完整回滚后才按file ID删除。这样即使最终目录已改名、回滚失败且后续错误marker也无法创建，仍有提前落盘的重启阻断依据。sidecar删除失败不撤销已接管运行，生产调用另报主/清理错误并保留阻断。
- `application.hpp/.cpp`覆盖任务、流程的publish后组装及提交入口，资源冻结与portrait cache沿同一协议；主错误与清理错误一起报告。未清理材料、身份未知及持久marker阻断后续准备和重启后的盲目重试；不按名称批量删除。
- Coordinator在改变active状态前完成可抛分配，worker经启动门闩等待明确接管。无新运行接口或隐藏fallback。

边界数值勘误：外部工作包P02假设1023总链接即达到上限，本机真实NTFS并非如此。实际`BundleLease::link_member`将1022→1023→1024均成功，下一次才返回3221226085。原测试失败保留于`wp01-publication-a1`；`wp01-link-probe-a1/link-boundary-probe.json`逐次记录真实file ID、nlinks和NTSTATUS。修复不根据预读nlinks分支。正式双别名发布从1023先增加一条、第二条失败再回滚，解释了原诊断看到的稳定1023，不改写历史证据。

验收覆盖真实NTFS容量、两个逻辑别名、已保留历史包的ID/SHA、失败回滚后的链接数、初始及回退复制块内取消、实际写/flush故障、非容量错误、预占位、不覆盖、创建身份失败、双错误及阻断、预算上下界/未知/溢出、中文空格及扩展长DOS路径。Application另测任务/流程publish后及units前故障、接管前失败、实际worker完成后返回故障、cache创建/复制/哈希/rename/清理拒绝。UNC未提供，明确未验。

## WP-02：请求历史准入和只读恢复

原因为写入只限字节，而启动另有条数限制；原子失败tmp又会使重建直接失败。

- `submission_store.hpp`统一维护条数、字节、内部tmp及未知状态，真实65,536条准入。原子提交成功才增加新条数，替换不增加；失败产物共同计量。
- 有界扫描遇到可信目录中的未完成/未知/超额材料，保留服务查询和旧ID按需验证，拒绝新意图。根/祖先不可信仍失败；不把未知或preparing重放成运行。
- Application先处理已有幂等查询，再核新写准入；运行状态带`request_history`诊断。原有冲突、300条重启回归保留。
- 测试用私有friend seam在真实atomic_write路径注入write/flush/commit，另测真实Windows共享拒绝rename及子进程commit前退出；失败旧JSON不变，tmp可诊断且计量。
- 真正创建65,535份小JSON，再保存第65,536份、拒绝第65,537份、替换并重建查询；不以伪造计数器代替真实文件边界。应用只读入口验证无准备、无新run、无设备输入。

## WP-03：全局recent配额

原因为先写后裁剪、删除失败仍继续写，tmp没有计量；重建RunStore又可能遗忘占用。

- `run_store.hpp/.cpp`保留原单recent worker：编码取得大小后，按可信目录身份串行核验全局最终图及严格内部tmp，成功回收/预留后再原子写。跨对象/进程使用同目录身份锁。
- 240张/128MiB、单图8MiB、待写4张保持。计量、裁剪或提交失败即sticky blocked；明确重新核验才可恢复。重建查同一目录，不清零额度。
- 必要诊断、输入和终态原职责保持。测试覆盖真实Windows拒删、数量/字节边界后连续20帧、write/flush/commit失败tmp、对象与子进程重建、实际目录身份替换、释放后显式重验、PNG/JPG/节流/去重及关键诊断。

## WP-04：资源生成与候选打包事务

原因为复制后才写journal，生成恢复丢索引；打包两次rename之间没有持久恢复；历史及失败材料未纳入准入。

- `resource_generation.py`内受限目录事务由资源生成和`package_functional.py`共用，各保留自身校验器。复制前持久记录意图、current/prepared/previous、身份与阶段；独占writer锁下先恢复上一笔，再允许下一笔。
- journal写/flush/replace失败及清理失败留证阻断；不得把未知目录按名称认领。保留历史，实际盘可用空间、完整stage、FS/事务余量和2GiB运行保留一起准入。
- 清单记录logical bytes与按file ID去重的文件长度，并明确后者不是exclusive allocated disk bytes。资源/候选历史均受有界清单核验，不套用原生16GiB批次限制。
- Windows实际测试包括copy/seed/generate/validate/journal/两次rename/最终核验故障、持久断点子进程退出、连续重试、拒删/清理阻断、低空间、reparse祖先与硬链接lock leaf。候选stage使用1KiB替身；真实切换及恢复函数没有替换。这不代表已完成产品候选打包。

## WP-05：服务启动绑定事务

原因为新服务未核成功就覆盖service-launch.json，失败启动可污染后续正式入口。

- `manage_service.ps1`以候选级共享writer锁串行，沿原入口先核新进程、PID/创建身份、owner与服务就绪，再原子提交绑定。
- 失败保留旧绑定字节；新进程已验证但绑定提交失败时准确报告新身份及提交失败，不自动杀掉或另起实例掩盖事实。未结候选打包事务阻止服务控制。
- 未结候选事务阻止Start/Deploy，显式Stop仍保留原生命周期入口，不能因打包材料未结而取消用户的停止权。
- 独立候选/data-root/本地替身实测launcher非零、后台早退、超时、owner/PID错误、两个data-root竞争同一候选，以及绑定write/flush/rename失败和真实共享拒绝。文件ID/SHA及实际进程身份在各case的evidence.json。

## WP-06：复核及实际验证

跨层发布由主实现负责，独立会话检查所有权；存储与工具实现交叉检查实际diff和失败材料。复核发现并修正：嵌套asset-object阻断遗漏、active状态前可抛分配、组装后提交异常缺口、portrait验证前过早接管、清理失败标志晚于诊断分配、已有cache绕过未结marker、工具事务祖先junction/lock leaf及绑定write/flush测试缺口。

| 新写路径 | 创建前准入/所有者 | 提交点与失败处理 | 重建行为 |
| --- | --- | --- | --- |
| 发布staging及最终目录 | 最坏空间；独占目录、确认身份、唯一token | 最终rename前flush准备索引；Coordinator明确接管；之前先放租约再撤回 | 不猜未知目录所有权；准备索引/残留阻断，无历史GC |
| 内容对象、冻结/portrait cache | 同次空间/闭包；同一目录token | 不覆盖rename+全验证后接管；双错误可读 | 未结索引/临时材料阻断，包括已有cache复用 |
| 请求JSON及内部tmp | 条数+字节；固定ID哈希及唯一原子tmp | 原子提交成功才记新条；旧JSON保留 | 有界核账；未完成/未知只读，旧ID查证不重放 |
| recent最终图及内部tmp | 编码大小+全局目录锁；计量/回收后预留 | 原子提交；裁剪或I/O失败阻断该可选writer | 同目录重计占用；明确重验后才恢复 |
| 工具journal及pending | 有界清单+完整峰值空间；writer锁 | 复制前durable意图；journal失败材料保留 | 未结journal先恢复；未知/清理失败拒绝下一事务 |
| 工具prepared/previous/current | journal关联、独占创建和身份 | 持久阶段推进两次rename；按已证实身份恢复 | 不删未知历史；旧current及保留历史验证/计量 |
| service-launch绑定tmp/JSON | 候选级同一writer锁；独立新进程核验 | 新PID/owner/Running证明后原子替换；旧绑定失败保持 | 准确报告新进程是否已启动，禁止自动另起掩盖提交失败 |

`tools/run_contracts.py --changed-from 01e5042 --plan`选择app、devices、recognition、runtime、tools、web，原生入口分别18/5/24/19；工具和web另执行。真实65k文件及多次真实模型组装用例保留在官方入口，单例超时600秒，其余原默认；服务绑定套件150秒。

最终聚合证据`next/.local/t0-repair-261025/final-verification.json`为complete=true：66个原生入口与12个工具/前端阶段全部有实际通过证据。工具阶段含WP04的26例、WP05的13例；不是12个底层测试，更不是游戏轮数。

最终产品输入SHA256为`043616242d1c9c18fa215d8736ead74c4aab3ce2215416d52a970b832a41c132`，1336项。a3的runtime19/devices5/recognition24共48入口已通过；其后仅两个发布测试文件的系统I/O检查改为长路径，原生产源码和八个相关测试二进制均逐项保持一致，见`a3-source-bridge-before.json`、`a4-source-bridge-after.json`及`unaffected-binary-bridge.json`。独立复核重算了全部1336项源码和八个当前EXE哈希，证实可复用这48项结果。

最终调用`python tools/run_contracts.py --module app --module tools --module web --output .local/t0-repair-261025/contracts-final-a4`，完整通过app18入口及工具/前端12阶段，回执complete=true；没有把a3失败的发布测试算作成功。a4深目录发布另有21项边界、准备生命周期13项、请求18项、recent11项实际证据。独立磁盘核对确认历史/私有复制的file ID及SHA、1024真实容量、复制内第三检查点取消、320字符普通DOS完整发布、重启后tmp计量与阻断。

17:45离线阶段终核：40项profile/workflows哈希、原交接文档和`.vscode/settings.json`保持；当时HEAD不变、索引没有暂存项。原PID22208、创建filetime134370861777540010、EXE SHA256为`1b71deb345ea58aac2f4c6281b0dac066236c7c73afc3d1ecdfffd80bfff740d`，run94 Completed/quiescent、repeat inactive、监控PAUSED。详见`protected-state-final.json`；该时点未部署或触发新轮，后续新授权执行另记。

### 证据索引（含保留的中间失败）

以下均相对于`next/.local/t0-repair-261025/`，完整日志在`next/.local/logs/`对应前缀。

| 证据 | 结论与边界 |
| --- | --- |
| baseline/ | 原文档与正式配置/服务只读基线；520项源码核对 |
| ntfs-native-link-limit-a1/、wp01-link-probe-a1/ | 独立原生调用及真实BundleLease两次证明1024边界 |
| wp01-publication-a1/ | 外部P02的1023上限假设失败，保留原材料 |
| wp01-publication-a2/ | 容量/回滚等断言先通过，随后junction错误码期望不符；后者实际被更早的身份门禁拒绝，保留原材料 |
| wp01-ownership-a1/ | OCR模型未纳入隔离manifest，准备钩子未到达；补齐锁定真实模型及fixture清单后重验，不改生产校验 |
| wp01-ownership-a2/ | 多次真实组装及实际接管后异常已推进，但120秒超时；保留中止staging，后续用600秒新根验证，未操作正式目录 |
| wp03-native-a1/receipt.json | 原生recent配额定向通过 |
| wp04-contract-13cdfaa4/tests.log | Windows工具事务26例通过 |
| wp05-contract-c09bfba0/tests.log | Windows启动绑定初版12例通过；最终13例另有有界回执 |
| wp04-bounded-final-1a8aec30/receipt.json | 最终工具事务26例经build.run通过，源码前后哈希一致 |
| wp05-bounded-final-7021f37d/receipt.json | 最终启动绑定13例经build.run通过，含未结打包事务下显式Stop，源码前后哈希一致 |
| contracts-final-a1/receipt.json | 统一构建及31入口通过，随后隔离manifest与原帧用例追加模型冲突，被BUNDLE_DUPLICATE_FILE正确拒绝。运行器改为ownership独立pack，其余保持原清单；后续重新统一验证 |
| contracts-final-a2/receipt.json | runtime19、devices5、recognition24及app9入口通过，含真实65k边界及13项组装所有权证明；完整发布测试发现深目录DOS临时名超260字符的真实缺口，保留失败并修复系统调用边界，追加普通长DOS回归 |
| contracts-final-a3/receipt.json | 48项非app入口及app9通过；生产长路径修复已生效，但测试注入对长路径存在性检查失效、未触发取消，发布测试如实失败。只修两个测试文件，不改生产参数或缩短夹具 |
| contracts-final-a4/receipt.json | 当前最终app18及工具/前端12阶段完整通过；源输入与二进制前后核对一致 |
| contracts-final-a4/publication/t0-boundaries/receipt.json | 21项真实Windows发布边界；长普通DOS完整发布、低空间/未知/溢出、双错误/持久索引均通过 |
| contracts-final-a4/preparation-ownership/results.json | 13项真实Application/Coordinator准备链；worker真实完成后目录仍在，重建App后新UUID仍被拒绝 |
| contracts-final-a4/submission-history/boundaries/results.json | 18项请求边界；上限目录独立清点确为65,536文件/7,394,452字节，替换及重建不增加条数、拒绝下一条不留tmp；其它故障子例的内部tmp保留并计量 |
| contracts-final-a4/recent-frame-quota/receipt.json | 11项全局配额；真实拒删、20次后续提交无增长、子进程重建、tmp计量、显式重验及必要诊断保持 |
| final-verification.json、protected-state-final.json | 聚合覆盖、源码/二进制桥接与正式现场终核；均通过 |
| formal-readonly-inventory.json | 正式历史只读清单：56份请求/29,550字节，未见内部tmp、未知项或preparing/unknown；published 1295项、asset-objects 3项、asset-cache 46项均未见准备阻断材料。只是有界只读盘点，不是新二进制正式验收 |
| protected-state-check-a1.json | 40项正式配置、原交接文档、.vscode及原服务状态与基线一致；监控PAUSED |

### 离线交付时边界（后续新授权见下）

修复不改正式profile/workflows、旧资产或`.vscode`，不清理正式发布历史。构建、完整候选打包、部署与实机是独立事实；本次没有部署、游戏输入、恢复循环/监控或commit/push。旧导航无进展及MuMu内部崩溃不在此次工作包修复结论内。

截至离线交付，新版本未在正式data-root运行；隔离测试中历史包保护及正式目录只读盘点不能替代部署前身份、配置及历史预检。离线阶段未通过重启旧服务试探修复。

离线阶段构建证据指官方合同入口的CMake聚合目标及前端构建，未生成发布BUILT回执。WP04的stage替身只验证真实事务/恢复，不宣称完整候选或游戏长期稳定；以下后续正式发布独立列证。

## 后续授权发布与新200轮（2026-10-25）

用户明确要求commit/push个人远端、构建打包部署正式服务，并在询问账目后确认“重新从0完成200轮”。不沿旧70/200、22/200或40/200续算，不恢复监控。

- 修复提交：`66568fb28c6693f4c39bb3c6d89562fe7e39fb51`，33文件，已推送`fork/codex/t0-lifecycle-repair-20261025`；`.vscode`没有纳入提交。
- 原完整`tools/build.py`完成依赖、资源、npm、CMake、web/native构建，`.local/build-input.json`为BUILT，产品输入仍为上述04361624…/1336项。默认打包被旧dist历史的交付格式校验拦住，保留原失败而不声明该命令整体成功。
- dist旁有97份历史，62份缺有效新式交付字段或完整标记。全部只读保留；原受限inventory核定98个根、87,334项、14,825,767,070逻辑字节，无不可计量或reparse项。没有替旧产物补标记、放宽验证或删除目录。
- 明确选取新版本目标`next/.local/t0-candidate-261025/`，通过原`package_functional.package()`及同一目录事务生成全新合法候选；额外保守准入含上述历史逻辑占用及元数据，要求17,545,527,633可用字节，实测145,372,815,360。该步骤未改生产源码，旧默认输出仍保留其原阻断。
- 完整候选含619个登记成员，pack revision `ae872c30e2608fac2075d727c84c8214c0d84dc40eb49712a9b659ff0aedbfa7`，EXE SHA256 `284e860b078f9e91599ac00f083b21d322359311c5118a1814ef9a0be1c3de34`。原管理入口Validate通过后，才执行Deploy。
- 旧候选独立回滚预检通过；41份profile/workflows文件（含元数据）已逐字节备份。部署前核旧run94 Completed、三业务单元、result_saved/quiescent、pending为空。原服务BAA009E9/PID22208经原shutdown入口确认退出，再启动同一data-root和17654上的新版；没有并行后台。
- 新实例`9F2AF8E0-F5BB-4EBC-BAF1-2DA16D6D7653`，PID47024、创建filetime134374039039026061。新服务已核owner/PID/EXE及running，`service-launch.json`成功绑定原正式data-root/17654；启动后请求仓库56旧记录、非只读、无未完成材料。profile/workflows及旧run94 SHA不变。
- 新根`giant-200-t0-20261025-b0ad41332a754a33b4d900f14f3b2424`，GiantBounty/zh-Hant/repeat_count200，原MuMu实例2，原profile revision不变。唯一请求提交前保存目标/配置哈希/身份和旧模型对象链接数；未重跑旧启动脚本。
- 20:18实际确认run1 Running、repeat active/target200/completed0；首轮已有1/3业务单元完成并进入generation2，实际backend输入17、拒绝0、pending为空、无连续异常。generation2是业务单元推进，不据此宣称游戏重启。
- 正式旧检测模型从1023增至1024链接后，第二别名实际成为不同FileID的独立复制；共享对象/两个别名SHA均为`8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9`。这是正式旧历史上的容量回退实证，不是新测试data-root替代证明。

发布证据：`next/.local/t0-release-261025/`的`release-confirmation.json`、`candidate-identity.json`、预检/部署日志、41文件快照与`rollback-plan.json`；新目标账本、唯一启动回执、实际运行确认及模型FileID/SHA证明在`next/.local/giant-200-t0-261025/`。监控仍PAUSED；未完成200成功轮，不宣称旧导航或模拟器问题已根治。
