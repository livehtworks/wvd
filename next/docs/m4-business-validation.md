# M4 部分实现与接续记录

当前快照：2026-09-15。实施基线 `4d7c4fa`；固定旧源 `6585f407`。
**M4 状态：M4_PARTIAL_IMPLEMENTATION。整个工作包仍在执行，不能以阶段提交或局部测试通过收口。**

当前已有数据/状态/策略、导航与入本、住宿与组队、有限战斗/开箱和类型化恢复的离线子流程；完整任务迁移仍未完成。各子流程最新证据见相应专题报告，不将历史测试数当作当前全量复测。
已按连续推进授权恢复执行，不等待再次授权，不重新跑 M0 或重做独立骨架。
未创建占位任务、空 handler 或假任务成功结果；生产仍为原 Python 唯一入口。

## 各阶段

| 范围 | 实际结果 | 剩余 |
| --- | --- | --- |
| M4.0 全功能盘点 | 保留 250 函数、33 配置、58 任务分母和来源/验收 ID；M3、数据、状态、计划数据分列；RuntimeContext 生命周期表已逐字段登记 | 250 函数完整语义复核、状态全部业务消费者及全部资源绑定交叉校验未完成 |
| M4.1 配置/mod | 严格复制导入、原树/passthrough、来源与默认值、导出、profile/CAS、基线任务和多 mod 冲突；33 字段区段组合、原生并发/锁冲突/替换失败、图片 mod 持锁发布和来源优先级通过 | 真实用户图片质量未验，其余非法组合和完整业务写回仍待核对 |
| M4.2 状态/策略 | 运行独占状态、正常续段、策略消费、重复遭遇幂等计数、分层恢复请求边界；恢复阶段 10 个状态方法通过 | 其余标志的完整业务消费者未接齐；见状态及恢复报告 |
| M4.3 任务编译 | 58 项类型化数据、43 项真实参数入本/路线/正常 Farm 迭代图；有限图发布、资源/权限绑定、普通插入；正常两段续接与局部预算、深度/作用域负例已验证 | 58 项完整业务的全局事件及专项 case 尚未齐；正常迭代不等于整任务验收 |
| M4.4 导航补给 | 地图搜索/到点/楼层、移动冻结出口、世界往返、入城立即停点、组队后实际住宿、角色面板恢复、入本及自动寻路；路线组合接入遭遇后的重识别/恢复；回城普通/强制补给和已确认住宿回执 | 剩余对话/完整任务循环及中断窗口对账未接齐；见 m4-dungeon-route-validation.md、m4-departure-validation.md |
| M4.5 战斗宝箱 | 单角色技能/等级/敌我目标、覆盖层与 Auto 确认、持续 Auto 有界等待、有限多角色遭遇；普通/quick 有限开箱完整链通过，含 40 次输入；Pause、单人/多人死亡提示、复活后恢复与继续路线已验证 | 其它全局事件、未知页旧盲点退路与全任务连接尚未齐；沙人/祝福提示实现待验，真实质量未验证 |
| M4.6 恢复生命周期 | 应用优先、重连/实例升级的类型化离线端口、启动就绪后继续原任务；首次连接失败 6 方法/7 场景、恢复全组 17 方法通过，见 m4-cold-start-validation.md | 核心回归执行中；全任务恢复位置、专项转交及其余冻结原因未接齐；真实恢复仍禁止 |
| M4.7 专项任务 | 15 个 quest 的原始数据保留 | 15 项专项 case 的真实原生业务与成功/失败/停止/恢复用例全部未实现 |
| M4.8 持久化诊断 | RunStore 已记录各有限段定义、业务摘要、检查点、逐代次输入计数及最终完成段数；离线配置副本 CAS | 完整业务计数、operation_id 写回、恢复对账与专项业务诊断未接入 |

## 配置与目录

真实原生数据检查当前 11 方法通过，包括全部 33 字段默认值、逐字段坏类型拒绝、
GENERAL/DEFAULT/任务专用覆盖、中文未知嵌套值、策略顺序、KARMA 字符串、
兼容导出修改、复制源不变、写入目标已存在失败、CAS 冲突和缺字段草稿拒绝。
58 个目录项逐项对照固定 Git 原始 JSON；重复 mod 冲突按旧顺序追加名称。

还从项目已知的旧运行配置路径执行一次只读复制导入：33 字段解析成功、复制字节一致、
源 hash 不变。真实配置全文与路径仅在私有证据，不进入公开报告或审核源码。
没有通过实例字段去连接模拟器、启动 VPN 或执行任务。

实现入口为 `native/app/m4_check.cpp`，调用 `LegacyConfigImporter`、`ProfileStore`、
`WvdQuestCatalog`；拒绝 device、binding、execute 请求。目录读取不创建 Maa 业务 Run。
数据结果 `outcome=PASS` 只表示本次解析/保存操作成功，同时明确返回
`execution_available=false` 和 `stage=M4_PARTIAL_IMPLEMENTATION`。

当前数据证据位于 `next/.local/m4-data-lnsubv1q/`；新增存储边界见 `m4-profile-boundaries-validation.md`，图片来源见 `m4-image-import-validation.md`。已知配置复制证据在本工作根的
`known-config-import/`，其公开摘要不包含配置内容。
字段和旧 RuntimeContext 范围见 [数据映射](migration/m4-data-mapping.md)。

## 任务分母

- dungeon：43 项，数据绑定通过 43，可执行任务 0，离线任务运行通过 0。
- quest：15 项，数据绑定通过 15，可执行任务 0，离线任务运行通过 0。
- 总计：58 项全部保留，release_allowed 全部 false，不把未实现项从分母移除。
- `m4-task-status.json` 逐个 task_id 保存源字段、哈希、原验收 ID、数据证据和未实现阻断；implementation_extent=TYPED_PLAN_DATA 不代表执行通过。
- `m4-implementation-map.json` 的 planned_owner 是旧规划，不是假装存在的入口；未实现入口为 null。

## 接续顺序

1. M3 发现链句柄归因和 CLEANUP_PENDING 所有权已达到本包有限修正边界，保留阻断，不再反复重跑；继续不依赖它的离线业务和完整性矩阵，不重测 M0。
2. 状态工厂、策略消费及正常续段已实现并通过离线状态断言；在业务接入时补齐剩余标志消费者，不把恢复机制当正常下一段。见 [状态报告](m4-state-validation.md)。
3. WvdTaskPlan 与有限子流程编译/发布已实现，继续完成全任务编译和原生回跳，不另建 VM。子流程范围及当前验证见 [流程报告](m4-workflow-validation.md)。
4. 完成导航/补给、战斗/宝箱及恢复动作，每条通过 GuardedAction/InputGate 的真实离线因果链；目标失败不消费策略，跨角色旧连点不续发。
5. 逐项迁移 15 个专项任务，接入业务计数、operation_id/CAS 写回和诊断；每项独立给出成功、失败、停止、恢复证据。
6. 补齐验收矩阵后提交新的离线阶段报告。用户已授权每阶段本地 commit；不含 push、真实连接/输入、M5 或旧版打包。

现有 100 项 M2 和通用视觉通过不代替上述业务验收。真实 NEXT/Pause、Tesseract 等价、
原生阻塞取消、RESOURCE_UNRESOLVED 和 Spark 仍保留原边界。

## 复现

```powershell
.venv-build/Scripts/python.exe next/tools/build.py --m4
.venv-build/Scripts/python.exe -m unittest discover -s next/tests/m4 -v
.venv-build/Scripts/python.exe next/tools/m4_inventory.py --data-evidence <本次m4-data目录> --state-evidence <本次m4-state-tests目录> --plan-evidence <本次m4-plan目录> --workflow-evidence <本次m4-workflow目录>
```

完整 `validate.py --m4` 包含已知失败的发现测试，不会因部分数据通过而给出总 PASS。
本次按独立组执行保留结果；没有跳过或禁用失败用例。不要为重复生成报告反复运行已阻断组。
