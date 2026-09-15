# M4 部分实现与验证边界

当前快照：2026-09-15。实施基线 `4d7c4fa`，固定旧源 `6585f407`。
**FIXES_INCOMPLETE / M4_PARTIAL_IMPLEMENTATION；完整任务验收 0/58。**
旧 Python 仍为唯一生产入口，全部任务 `release_allowed=false`，停在 M5 前。

本次仅修正文档及静态盘点，不运行原生测试、构建、设备或网络，不提交/推送。
下列最新结果由用户/主代理提供；本审计没有重跑、补造或替主代理证明通过。
主代理专题报告保留失败轮次和实际产物身份，是分项证据入口。

最终交回状态：主代理已冻结全部源码并构建中；unknownLeap、publisher多图及boot映射已接入
新版真实调用链。新增源码仍无构建完成或测试通过结果，不是旧Python生产入口切换。

## 各阶段

| 范围 | 当前实现 | 未完成或阻断 |
| --- | --- | --- |
| M4.0 盘点 | 250函数/33字段逐项补固定旧引用、消费者、差异分类；58任务分母保留 | 静态盘点完整不等于语义等价完整；动态属性/回调及全矩阵未验证 |
| M4.1 配置/mod | 原树、来源、passthrough、区段合并、严格解析、显式副本/CAS、任务mod与持锁图片发布 | 9字段仅导入/导出；24字段有范围的读取，不等于全部业务承接；用户图片真实质量未验 |
| M4.2 状态/策略 | Run独占状态、正常续段、策略深复制/消费、任务步、遭遇/住宿/复活确认 | 全任务恢复、配置组合与副作用对账未齐；新增扩展/转交状态未构建不可认领通过 |
| M4.3 编译 | 58类型化目录，43普通入本/路线/迭代图，15基础quest专项源码；发布绑定权限/资源/注册表 | 新多阶段发布已接线/构建中，离线fixture使用同sealed revision的多namespaced pipeline；完整任务0/58 |
| M4.4 导航补给 | 世界往返、入城停点、EOT、地图/AutoMove、组队后住宿、角色恢复、付款意图保护 | 全任务循环/恢复位置、局部路线和配置/mod组合未闭合 |
| M4.5 战斗宝箱 | 头像/技能/等级/目标、Auto覆盖层/确认、多角色遭遇、普通/quick开箱、Pause/死亡/复活 | 真实NEXT/Pause/Tesseract质量未决，不能用子流程代替全任务组合 |
| M4.6 恢复 | 应用优先/连接/实例升级的类型化离线请求、冷启动、默认/专项对话、未知窗/MAX_TRY/400秒出口 | 真实生命周期端口未接；转交/7300秒等待新源码及全部恢复矩阵待完成/验证 |
| M4.7 专项 | 15基础专项均有代码；陷阱、挖矿、住宿、蝎女/六手、钓鱼补饵、吉尔有专题分项证据；7000G最新两分支通过 | 巨人/暗灯/半自动大恶魔/忍洞BLOCKED；牛洞、沙人、钢试炼最新失败保留；四源码扩展新改动未构建 |
| M4.8 结果/写回 | RunStore逐代次定义/摘要/输入/检查点，善恶确认后对新版profile CAS；新TaskHandoff/长等待已接线、构建中 | 不等价旧Tk日志/PNG/更新器；全部对账、转交最终调用链及完整回归未完成 |

## 最新验证

以下绑定用户明确给出的旧产物及修订，不可追认为之后新增源码的结果。

| 产物/证据标识 | 已提供事实 | 结论边界 |
| --- | --- | --- |
| `695295d` 加沙人窄入口/专用对话同步批处理产物 | M4状态36方法、计划9方法、M2 104方法通过 | 不是当前新增扩展、状态、转交或多阶段发布的构建/回归证据 |
| `m4-specials-correction-workflow`，root `7g_0bqno` | 专项11方法：4通过、5失败、2 Error | 保留整批真实结果，不以4通过覆盖失败 |
| 同批7000G | 完整剧情两分支23/24输入PASS；停止/拒绝各1 PASS | 仅这些分项，完整任务0/58不变 |
| 同批牛洞 | 28输入后约2030ms帧龄处门禁SCENE失败；REST=true未执行 | 失败与未执行分开，不写牛洞通过 |
| 同批忍洞修正2 | 13输入后仍SCENE失败，达到有限修正边界 | BLOCKED，不再重跑选PASS |
| 同批沙人/钢试炼 | 沙人两个完整例因夹具图像自检失败，非原生失败；钢试炼发布缺bondmate_close，零连接/输入 | 夹具与原生失败分开；后续复验见下一行 |
| 旧产物root `vg19i0vi` | 用户原样提供：沙人 `noBond Failed 3`、`Bond Failed 0`；钢 `full Failed 5`、`stop Failed 4`、`UserStopped 4`；全部5项已核对EXE/hash/quiescence | 保留标签/计数及失败，不把静止或UserStopped解释为完整业务成功 |
| root `gqgh3uz1` | 因果/吉尔对话/Golden对话/时间跳跃17方法、24场景全PASS；23真实Run加1缺图发布负例，已核对EXE身份/计数/静止 | 对话子图通过不解除Golden完整任务BLOCKED，不覆盖新源码 |

专题入口：[7000G](m4-gold-income-validation.md)、[牛洞](m4-bull-cave-validation.md)、
[忍洞](m4-golden-chest-validation.md)、[沙人](m4-sandman-validation.md)、
[钢试炼](m4-steel-trial-validation.md)。本次未修改这些报告。

先前陷阱、挖矿、住宿、蝎女/六手、钓鱼补给、吉尔分项保留各自报告与当时构建身份。
住宿真实Maa仅40次单批证据，状态9999回执/250批次不替代真实长跑；挖矿未知后置仍未闭合。
巨人/暗灯/半自动大恶魔继续保留既有有限修正后的阻断，不追加重跑。

## 实现与未验证

- **缺消费者/未实现入口**：9配置字段只有导入/导出；旧通用存仓、PNG持久化、Tk编辑/日志展示、
  更新链无等价新版入口。逐字段/函数定位见[语义审计](migration/m4-semantic-audit.md)。
- **已有代码但未构建/未验证**：Repel/COS/Fordraig、相应状态/恢复、转交/7300秒等待，以及
  `publish_workflow_stages` 和CLI/fixture接线源码已冻结、构建中，不能标PASS或写“完全没有实现”。
- **已有运行证据但未通过/范围不足**：牛洞/沙人/钢试炼/忍洞最新结果及三项原阻断分别保留；
  单场、对话、状态或计划通过不是完整任务验收。

多阶段发布遵守同一个sealed bundle revision，各正常段独立entry/checkpoint/time/dialogue
binding，保留runtime现有同revision限制，不以放松约束或并行运行器完成接线。
4个源码扩展为 `fordraig/repelEnemyForces/CaveOfSeperation/steeltrail`，不改基础58项。

## 配置与目录

`native/app/m4_check.cpp` 已调用导入、profile保存、目录/计划、普通和专项图编译、资源检查与
准备期模板展开；仍拒绝device/binding/execute，不创建Maa Run，输出
`execution_available=false`。不再写成只有目录读取，也不能把编译当执行。

显式新版profile由LegacyConfigImporter/ProfileStore维护来源、原树和CAS，Run冻结values。
善恶确认写回已实现，不再写“所有业务写回未实现”。其它字段消费者不由导入PASS推导。
旧config/mod不写；原生图由离线fixture经publish_workflow、封存Registry与RunCoordinator执行，
M1服务仍只读，未开放M5运行API。

历史数据全组12方法通过对应 `4b408ea` 与 `m4-directory-data-regression.log`；
私有证据 `m4-data-0nfes3vr`、早期 `m4-data-lnsubv1q` 保留。已知用户配置复制/源hash核对的
历史事实不等于本轮重新读取或使用用户配置。字段详情见[数据映射](migration/m4-data-mapping.md)。

## 分母与权威

- dungeon43、quest15，共58项保留，**完整离线任务通过0/58**。已有有限图/可执行子段，
  不再用含混的“可执行任务0”否认代码存在。
- `migration/m4-task-status.json` 是逐任务验收权威，本次不写它。早期数据extent
  不能覆盖后来源码存在的事实；当前静态去向在 `m4-implementation-map.json`。
- 函数/字段 `semantic_audit` 是本轮静态权威；原offline_status仅保留历史局部证据，
  并非本次新测通过。历史M1不改，不运行m4_inventory.py覆盖人工语义结论。
- M3 Metadata/句柄/CLEANUP_PENDING保持BLOCKED；RESOURCE_UNRESOLVED、
  PERFORMANCE_UNRESOLVED、真实NEXT/Pause、Tesseract等价、系统导航、SDK内部重连、
  任意原生阻塞取消和Spark继续保留。

主代理独占运行中的测试，本次文档分工不执行构建/验收命令，不以静态检查代替测试。
