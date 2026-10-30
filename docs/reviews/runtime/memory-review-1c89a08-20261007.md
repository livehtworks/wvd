# 1c89a08内存审查落实

本文件保留M01–M03限定阶段证据；用户给出一小时后追加的P01–P04当前事实及跟踪负证据见[准备链闭环](memory-preparation-closure-20261007.md)。下文未启动M04与P01后续描述属于当时阶段，不是新增P01未完成的结论。

## 范围

已核对用户下载的`wvd-memory-codex-review-1c89a08.zip`、00入口、各阶段要求及8份校验和。基线`1c89a086f013bb3968974124661b0eacbd4f78bf`。本次按包先处理M01–M03，不启动M04，不修改P01发布器、P02连接链、P03前端计时或P04保留策略；这些需按包单独变更、验收，不混进同一内存对照。未部署、commit/push或操作游戏。

M01–M03定向验收通过。M02追加的“已生效后超时”管理员实测首次被取消；用户要求重试后已实际通过，历史失败保留。M04身份预检通过，但两轮时间预算不能通过，因此没有启动新产品采集。历史45.11MiB继续RESOURCE_UNRESOLVED。

## M01直接聚合

唯一堆分析入口`export_memory_stacks.ps1`已切到独立x64 TraceProcessor 1.12.10，依赖锁定；不再调用WPA导出海量明细或将明细CSV整体装入PowerShell。先筛目标实例/位数，再以完整模块身份+RVA/地址序列累计叶级块数、字节；每个唯一栈只输出一次。保留负增量、零增量，检查64位溢出及全部合计守恒。

正式输出为包内五种文件，另有最终统计、符号装载及缺符号覆盖。时间匹配逐快照实际UTC落入唯一命令区间，不按数量和序号猜phase。PowerShell反序列化可能将ISO时间变为DateTime；现用保留Kind的DateTimeOffset转换，修掉曾经再次字符串Parse导致丢UTC偏移的误拒。旧原回执、ETL、冻结profile不改写。

| 实际输入/断言 | 结果 |
| --- | --- |
| 包内HeapAlloc专用夹具 | AllocKeep +4/+16384B；AllocRelease -3/-6144B；AllocStable 0；4096次128B AllocChurn两端0；专用栈净+10240B |
| 夹具计量 | HeapSize核对请求字节，指针存储预分配；基建分配分列，不强求全进程差等于专用栈差 |
| 第一次夹具失败 | 数值正确，但尾调用抹去函数名；已保留旧证据，以volatile后置使用抑制尾调用，未改验收数字 |
| 最终夹具分析 | 1.512秒，峰值私有提交194990080B，输出约745KB；WPR/PID配置均确认关闭 |
| 原41.814MiB ETL | 最终分析7.747秒，峰值私有提交886988800B，全部输出114858440B（109.54MiB），在1024MiB/120秒/128MiB预算内 |
| 实际快照 | #9：135402块/12096893B；#10：84976块/4888800B；各栈、总量和原始相邻差分守恒 |
| 最终ETL统计 | 文件完成，EventsLost=0、BuffersLost=0；原capture_complete=false保持，comparison_eligible=false、attribution_complete=false |
| 输出预算 | 400个中文字超1024B的真实UTF-8写入被拒，保留.partial、不发布成功文件 |
| 内存预算 | 隔离128MiB Job Object实验拒绝继续分配，峰值私有提交127811584B；不是仅语法检查 |

原文件分析SHA256：`8BCB7EA961E9CD452E5E0E101648E66E8E9A2E3149B152B9F2A6271DDD89503F`。最终分析DLL SHA256：`8C743C6C244E9FE4A9A3BFE099DF1E66D9076174A5BA67FE0FA4E33C1A7D365E`。SDK内部仍缓冲数据，不能说常量内存。应用符号按匹配PDB加载；系统DLL未解析逐模块列出，模块覆盖不可相加。自有输出写前字节门禁和Job内存限额为硬约束，原生符号缓存/外部工具目录轮询不是硬磁盘配额。

证据：`next/.local/memory-review-1c89a08/known-733e94c02b85442f9bee6dd1e742d2d7/`、`budget-case.log`；真实文件结果在原证据目录的`m01-final/`及`m01-final-receipt.json`，不是原receipt覆盖。

## M02边界和清理

- Monitor前后核对waiting、active、同批次/计数/run/generation/目录及新鲜服务和OS创建身份；busy=true的正常waiting允许，starting或批次释放拒绝。保存OS前后私有提交和时间，不把较早worker_joined标量冒充快照同刻。
- Collect统一标`batch_payloads_released_1/2`，读取对应生命周期。核对保存终态、代次、instance/目录、诊断失败计数及样本进程身份；流式复查落盘恢复事件，低频UI没看见恢复不算无恢复。
- 常规current查询降为5秒缓存，关键边界强制新读；每张图前同样静默1秒，不在该区间后台查询。记录实际请求起止及其它观察者未知，不声称GET返回就等于服务端响应析构。
- enable/start之前标记attempt，finally查询实际状态；由共用Close-OwnedTraceCapture只清理本次唯一session和匹配创建时间PID，未尝试的配置不动、PID复用不碰。日志写入失败也不绕过mutex释放。
- 命令、绝对截止、取消及所有异常均有界收回本次子进程；不使用无参数WaitForExit。只stop一次，失败后有归属cancel，不重复两个120秒stop。预留180秒，剩余时间不足不提交第二轮；清理超出截止记录违反而非跳过。
- stop之后先确认session/PID配置关闭、落盘cleanup，再进行独立轻量最终ETL检查；未知不写0，最终非零丢失不允许差分。读取原437MiB失败ETL实测136897丢事件，明确拒绝。

15项定向guard通过：包含旧Completed字段不变的waiting→starting、新用户Collect请求、代次变化、批次释放、最终非零/未知/未完成、真实子进程超时/绝对截止/取消/文件查询异常、活动输出超限与过期截止拒绝。生产模块负责实际验证，无测试专用生产分支。

另有隔离管理员实测：enable已生效后日志路径错误、start已生效后日志路径错误，真实配置/ETW logger探查与清理均成功；外部命名WPR会话被拒、未拥有会话未动、错误创建时间未关闭PID配置。第一次helper进入已知分配阶段后因尾调用夹具验收失败，原清理仍confirmed；相关命令/状态日志保留在`capture-47899b2374b8492abc8151e4762063ef/`。

用户明确要求重试后，管理员助手PID52836实际运行`OnlyOwnershipCases`，四项全部通过：enable已生效后日志失败、start已生效后日志失败、start已生效后包装超时、外部命名会话及PID创建身份保护。超时项保留`start-applied`标记、实际logger探查及`stop-incomplete.log`保存成功证据；全部cleanup_confirmed=true，最终session/config关闭、助手和夹具进程退出，未留下命名WPR会话。证据目录：`next/.local/memory-review-1c89a08/ownership-609d803a82b24620ab4090e183507d21/`。此前取消启动PID为null的历史记录不改写。

新的完整产品Collect未执行；真实系统异常造成清理不能按期完成的情况未触发，不能保证任意OS故障都在20分钟内完成清理。

## M03状态复制优化

生产窄改仅EventJournal::read和Application::run_status：直接构造最终页，临时返回值移动入响应，扫描借用响应字段。事件环仍1024/终态1025，cursor、critical、锁和接口不变。独立benchmark使用同一不可变实际事件页及旧复制模式参考，不变更生产代码以注入计数。

1024条嵌套事件、每组10次、序列化624609B；每次响应销毁后才进入下一次测量，计数分配和释放完全配对：

| 口径 | 旧分配字节/次数 | 新分配字节/次数 | 本机10次墙钟 |
| --- | --- | --- | --- |
| Journal页（含非竞争锁/构造/销毁） | 59772850 / 963120 | 30116400 / 481800 | 119.24→55.65ms |
| 事件响应装配片段 | 89538080 / 1444680 | 30118640 / 481850 | 155.71→49.85ms |

事件响应片段分配字节减少约66.4%，不是整套HTTP或游戏吞吐提升66.4%。真实Application公开状态接口的事件页、state及reverse scan验证通过；空环、cursor、淘汰resync、critical、满环终态、提交失败保持原历史、并发emit/read均有断言。锁内单独CPU成本未加生产埋点，不将整个HTTP时间称锁占用时间。结果在`status-case-final/result.json`。

## 现有证据和剩余项

原部分join快照前五个存活栈各1024块/81920B，均有NativeRunCoordinator::drive→EventJournal::commit_terminal。对应当前有界终态事件环的复制保留，需到下一次替换journal的生命周期解释；这是存活排名，不是主要净增长或泄漏根因，未顺带改commit_terminal。

M04已执行只读身份Preflight，`complete=true`、side_effects_attempted均false、rounds_submitted=0；原candidate121、匹配PDB及Interrupted/quiescent现场一致。证据：`next/.local/memory-review-1c89a08/m04-preflight-eb7972a74dc84940a19cfa2c4c79afcf/receipt.json`。它只证明采集入口身份和未占用状态，不证明预算或分配差分有效。

时间预算仍不放行：上一真实跟踪窗口记录准备至少约480秒，当前已完成run18的业务执行340.5249511秒；按同一负载保守估算`2*(480+340.5249511)+180+3*33=1920.0499022秒`，约32分钟，超过1200秒。准备数据与run18不是同一次分段测量，不能称精确预测；它足以表明目前没有两轮满足20分钟的证明，不能只用执行5分40秒填EstimatedRoundSeconds、删掉准备或扩大授权预算。未启动Collect，未再取一个注定缺B2的部分窗口。按包下一步应独立完成P01分段计时/有界模型复制，取得新的准备实测后再评估M04。P02冷启动、P03计时watch、P04只读磁盘盘点仍为独立后续；没有以本次局部优化替代它们。

本次收口只读现场：candidate121仍为原后台，巨人原50轮已完成17轮，run19 Interrupted/EVENT_PARENT_RESULT_TIMEOUT、quiescent且落盘，repeat inactive/failed。未恢复循环或处理这条业务异常，未把它归因于此次分析。用户配置、旧失败产物和`.vscode`保留。
