# 架构审核整改交付

## 结论与范围

基线4c466e14fa514fa177380e902afe373c8f20c3b2。审核包54个文件大小/SHA256一致，见[校验记录](architecture-audit-20261010/evidence/package-verification.json)。16个工作包的源码整改/取证准备已交付，最终冻结候选实际执行的检查全部通过；**不是16包全部验收关闭，更不是资源归因、部署或实机通过。**

保持Application → Coordinator → Session → FlowExecutor唯一链；没有改ROI、阈值、帧TTL或全局短路吞Error，没有平行执行器/旧链回退。正式profile、用户保存流程、config/mod、部署、后台、模拟器和游戏均未操作。本轮未commit/push，implementation_commit=null；HEAD仍是审核基线，修改在工作区。

逐项原来源、当前实现和证据见[43项索引](architecture-audit-20261010/ISSUE_INDEX.md)、[结构化清单](architecture-audit-20261010/ISSUES.json)和下表16份RESULT_SCHEMA回执。没有将43项批量标closed。

## 分包结果

| 包 | 已落实与边界 | 验收状态 |
| --- | --- | --- |
| [WP01](architecture-audit-20261010/work-packages/WP01-result.json) | 调用前登记输入，送达后分配失败不丢pending；五故障和既有回归通过 | 限定离线合同通过 |
| [WP02](architecture-audit-20261010/work-packages/WP02-result.json) | 实例创建/连接/事务/时间因果证明；生命周期取消与绝对期限优先；协调器拒绝旧证明 | 限定离线合同通过 |
| [WP03](architecture-audit-20261010/work-packages/WP03-result.json) | 局部资源即时RAII、固定清理槽、线程异常边界、先注册watcher；逐SDK HANDLE故障尚未穷尽 | 源码已落实；包级验收部分 |
| [WP04](architecture-audit-20261010/work-packages/WP04-result.json) | 唯一策略继承、未知组拒绝、先身份再剩余动作、统一Karma解析；配置/频次/保底通过 | 限定离线合同通过 |
| [WP05](architecture-audit-20261010/work-packages/WP05-result.json) | scene/target均需Hit+eligible；独立BusinessConfirm条件；重查不重置期限；缺疗效素材保留Unconfirmed | 限定离线合同通过 |
| [WP06](architecture-audit-20261010/work-packages/WP06-result.json) | 默认关闭的同阶段屏障、统一端点资格，完成保存/哈希纳入期限；未做真实短预检 | 限定离线合同通过 |
| [WP07](architecture-audit-20261010/work-packages/WP07-result.json) | 稳定unknown桶、signed栈净增/守恒、有界输入输出和Job回收；真实ETL投影/覆盖仍缺 | 源码已落实；包级验收部分 |
| [WP08](architecture-audit-20261010/work-packages/WP08-result.json) | 有界叶子/同帧单生产者、事件scope缓存、语义进度、补给阶段计划；缺繁中完整现场与语言切换 | 源码已落实；包级验收部分 |
| [WP09](architecture-audit-20261010/work-packages/WP09-result.json) | CTC形状/完整类别、直接ROI、ResultOnly、根run模型池；真实ORT通过，复杂多框完整成本未量化 | 源码已落实；包级验收部分 |
| [WP10](architecture-audit-20261010/work-packages/WP10-result.json) | 高成本解码/模板/掩码/OCR压力票据；系统commit/VA/已知Job cap独立；估算不是RSS | 限定离线合同通过 |
| [WP11](architecture-audit-20261010/work-packages/WP11-result.json) | 单ReadView、cursor、绑定instance/run/gen/id的图，urgent停止与stop_epoch；并发/延迟PNG端到端未穷尽 | 源码已落实；包级验收部分 |
| [WP12](architecture-audit-20261010/work-packages/WP12-result.json) | 准确backup、body/meta事务、GET只读、可变版本检查前持久幂等；四中断/300回执/三API重试通过 | 限定离线合同通过 |
| [WP13](architecture-audit-20261010/work-packages/WP13-result.json) | 不可变模型共享、2GiB准入/16GiB批次、关键图独立额度、辅助32MiB；16GiB完整边界未验证 | 源码已落实；包级验收部分 |
| [WP14](architecture-audit-20261010/work-packages/WP14-result.json) | 消费依赖字节锁、生成事务/可核原始素材补全、stage只读、builtin隐式模板/OCR闭包；最小包未穷尽 | 源码已落实；包级验收部分 |
| [WP15](architecture-audit-20261010/work-packages/WP15-result.json) | 五核心合同、模块验收/源及EXE身份、fork PR CI、59目录与休眠边界；远端CI/全部旧正向反例未执行 | 源码已落实；包级验收部分 |
| [WP16](architecture-audit-20261010/work-packages/WP16-result.json) | 三角色身份/自然崩溃限额与截止的UNARMED计划；缺libRenderer dump/fault栈/符号/厂商契约 | 计划通过；根因未解决 |

## 实际验收

实际执行：

```powershell
C:/Python314/python.exe next/tools/run_contracts.py --module runtime --module devices --module recognition --module app --module tools --module web --output next/.local/architecture-audit-20261010/final-contracts-14
next/build/Release/test_head_response.exe
git -c core.safecrlf=false diff --check
```

完整入口含39次定向原生合同命令及11项工具/构建命令，**不是50轮游戏**；12组原有Flow回归保留。源hash在构建前后/验证末尾一致；10个原生EXE、4个分析器产物身份已登记。C# locked依赖还原、聚合及Vue构建通过。额外HEAD检查通过，正文为零，声明长度与GET一致。

[最终回执](architecture-audit-20261010/evidence/final-contracts-receipt.json)、[实际输出与日志索引](architecture-audit-20261010/evidence/final-contracts.log)、[证据哈希](architecture-audit-20261010/EVIDENCE_MANIFEST.json)。本报告目录复制实际小日志/夹具结果，没有EXE、用户配置或游戏原始截图。

消费源身份：`56a9ac97f30875dfd18931f573d2fe57d0616f60f29924111796a1e3a36d242e`。HEAD只标来源基线，不能解释为修复已提交。回执文档hash是执行时快照，随后补齐交付文档不会改变产品输入hash。

- [WP01旧正向失败](architecture-audit-20261010/evidence/wp01-before.log)实际报SENT_RECEIPT_LOST；[当前五故障](architecture-audit-20261010/evidence/wp01-after.log)保留未决事实。[四旧反例JSON](architecture-audit-20261010/evidence/baseline-counterexamples.json)准确重现坏结果，但exit0不是正向验收。除WP01外没有将审核反例来源包装成本轮旧正向重跑。
- 中间失败也保留：硬链接共享冲突、dotnet未解析实际EXE/默认源缺包、npm批处理被CRT转义破坏。最终从冻结句柄链接、PATH解析、README既定locked源、同Job的cmd原始字符串规则修正。19个Job守卫含空格路径/参数和exit41，仍走同一原子Job归属。
- 原稀疏fixture缺returnText并非仓库永久缺图：`returnText.png → ReturnText.png`别名与真实原始字节可核验补全；缺失、内容改动或模型不在manifest仍Error拒绝，不用假图填洞。正式素材没有改写。

## 补给识别量化

同一900×1600合成帧包含真实英文trait模板；旧context/panel/post配方与新阶段计划都经当前生产Service计算。**不是旧/新EXE对比，也不是繁中现场Heal完整段。**

| 指标 | 旧配方 | 新计划 |
| --- | ---: | ---: |
| condition visits | 107 | 9 |
| actual matches | 48 | 10 |
| eligible unique leaves | 8 | 5 |
| C++ new/new[]次数 | 93667 | 9337 |
| C++ new/new[]申请字节 | 4884352 | 448836 |
| 单样本墙钟毫秒 | 530.7676 | 299.9307 |

[原数字与范围](architecture-audit-20261010/evidence/supply-plan-metrics.json)。分配计数不覆盖malloc/OpenCV/ORT内部、对齐分配或保留堆；墙钟仅单样本。不能推导历史46.352秒/1199次现场已修复或泄漏已消除。

## 不关闭的事项

1. **RESOURCE_UNRESOLVED**：本轮无真实同阶段ETL/heap snapshot。合成[栈净增表](architecture-audit-20261010/evidence/heap-fixture/heap-delta-by-stack.csv)明确fixture；真实增长栈、owner、释放位置、符号覆盖尚未交付，free/VA也不具备。工具通过不等于归因；真实短窗口须另行授权，先满足端点/预算/丢事件/覆盖资格，再追实际top候选，不扩轮碰运气。
2. 自然libRenderer/0xc0000409缺dump和fault栈；旧nvoglv64与主动恢复分开。UNARMED计划未启动；厂商契约未证实前，SDK调用次数/尺寸策略/缓冲复用不改。
3. 疗效结果、面具技能面板及部分随机事件缺真实材料；保持各自未确认。返回迷宫不是治疗效果，不再用generic healing_completed洗成成功；没有禁用59目录或虚构悬赏领取。
4. WP03/07/08/09/11/13/14/15仍有表格和JSON中的必需覆盖缺口。`positive_assertions_pass_after_fix=null`表示包级未完全证明，`executed_targeted_assertions_passed=true`仅指实际执行的命名断言。
5. 没有正式迁移/部署/实机/连续100轮回执。正式保存流程优先于源码默认；未来切换需逐一核正式revision、准确backup与CAS，不覆盖用户编辑。本包不授权这些操作。

## 当前入口与保留

[核心合同](../../next/docs/core-contracts.md)、[59项范围](../../next/docs/public-task-scope.md)、[数据职责](../data-authority.md)、[当前事实](../project-status.md)、[执行注意项](../execution-notes.md)。

requests、result、输入、诊断、同步backup是事实，不自动清理；模型是派生内容但受活跃租约保护。2GiB可用/批次16GiB只是准入政策，不是物理预分配；到界停新轮，不删除历史换空间。归档前须只读盘点引用/租约、准备可恢复备份并取得明确授权；本轮没有归档正式资产。

句柄链接依据[Microsoft FILE_LINK_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_link_information)，批处理引号依据[Microsoft cmd /s /c](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/cmd)。只对未公开暂存目录允许成员创建；源与已发布文件的不可写/不可删保护没有解除。
