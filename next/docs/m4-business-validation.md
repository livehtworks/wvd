# M4 部分实现与接续记录

日期：2026-09-14。实施基线 `4d7c4fa`；固定旧源 `6585f407`。
**M4 状态：M4_PARTIAL_IMPLEMENTATION。整个工作包尚未完成，整体按 FIXES_INCOMPLETE 收口。**

当前已有 M3 部分修复、资源快照核心、M4 数据层及运行状态/策略，以及入城、住宿和 Auto 有限子流程；全部业务迁移仍未完成。
已按连续推进授权恢复执行，不等待再次授权，不重新跑 M0 或重做独立骨架。
未创建占位任务、空 handler 或假任务成功结果；生产仍为原 Python 唯一入口。

## 各阶段

| 范围 | 实际结果 | 剩余 |
| --- | --- | --- |
| M4.0 全功能盘点 | 保留 250 函数、33 配置、58 任务分母和来源/验收 ID；M3、数据、状态、计划数据分列；RuntimeContext 生命周期表已逐字段登记 | 250 函数完整语义复核、状态全部业务消费者及全部资源绑定交叉校验未完成 |
| M4.1 配置/mod | 严格复制导入、原树/passthrough、来源与默认值、导出、profile/CAS、基线任务和多 mod 冲突；状态工厂参数随 Run 冻结 | 全部组合、并发/磁盘失败矩阵、用户图片 mod 接入未完成 |
| M4.2 状态/策略 | BusinessRunState、WvdRunState、版本化工厂；正常有限段检查点续接；策略/计数/单调计时；新增普通/强制补给条件断言，共 7 个状态测试方法通过 | 其余业务消费者仍未连接，部分旧标志留在对应业务迁移范围；生命周期逐项见状态报告 |
| M4.3 任务编译 | 58 项类型化数据；PipelineCompiler 校验有限图、权限和资源索引；publish_workflow 封存复制并绑定源/图/注册表修订，已有子流程入口 | 尚未把 58 项任务计划编译成完整任务图；原生回跳和专项 case 未实现 |
| M4.4 导航补给 | 已有目标可见时入城和旅店主路径的实际 Maa 因果验证；补给策略区分普通间隔与强制补镐 | 开地图/缩放/滑动、楼层/路线/移动停止、住宿后旧坐标对话、重组队伍及调度尚未完成 |
| M4.5 战斗宝箱 | Auto 子流程先关覆盖层、识别未知时有限保底、确认开启；弹窗卡住有恢复出口 | 技能、NEXT/敌我目标、死亡、开箱、战斗结束与策略成功消费的完整输入链仍未完成 |
| M4.6 恢复生命周期 | NOT_IMPLEMENTED | 业务恢复决策、旧 Session 退出、新代次与场景重观测；真实恢复仍禁止 |
| M4.7 专项任务 | 15 个 quest 的原始数据保留 | 15 项专项 case 的真实原生业务与成功/失败/停止/恢复用例全部未实现 |
| M4.8 持久化诊断 | RunStore 已记录各有限段定义、业务摘要、检查点及最终完成段数；离线配置副本 CAS | 完整业务计数、operation_id 写回、恢复对账与专项业务诊断未接入 |

## 配置与目录

真实原生数据检查的 8 组 unittest 均通过，包括全部 33 字段默认值、逐字段坏类型拒绝、
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

逐项证据位于 `next/.local/m4-data-wcgidsuy/`；已知配置复制证据在本工作根的
`known-config-import/`，其公开摘要不包含配置内容。
字段和旧 RuntimeContext 范围见 [数据映射](migration/m4-data-mapping.md)。

## 任务分母

- dungeon：43 项，数据绑定通过 43，可执行任务 0，离线任务运行通过 0。
- quest：15 项，数据绑定通过 15，可执行任务 0，离线任务运行通过 0。
- 总计：58 项全部保留，release_allowed 全部 false，不把未实现项从分母移除。
- `m4-task-status.json` 逐个 task_id 保存源字段、哈希、原验收 ID、数据证据和未实现阻断；implementation_extent=TYPED_PLAN_DATA 不代表执行通过。
- `m4-implementation-map.json` 的 planned_owner 是旧规划，不是假装存在的入口；未实现入口为 null。

## 接续顺序

1. M3 发现链句柄归因和 CLEANUP_PENDING 所有权仍需解决；完整性矩阵与成本缺口单列，不重测 M0。
2. 状态工厂、策略消费及正常续段已实现并通过离线状态断言；在业务接入时补齐剩余标志消费者，不把恢复机制当正常下一段。见 [状态报告](m4-state-validation.md)。
3. WvdTaskPlan 与有限子流程编译/发布已实现，继续完成全任务编译和原生回跳，不另建 VM。子流程范围及当前验证见 [流程报告](m4-workflow-validation.md)。
4. 完成导航/补给、战斗/宝箱及恢复动作，每条通过 GuardedAction/InputGate 的真实离线因果链；目标失败不消费策略，跨角色旧连点不续发。
5. 逐项迁移 15 个专项任务，接入业务计数、operation_id/CAS 写回和诊断；每项独立给出成功、失败、停止、恢复证据。
6. 补齐验收矩阵后提交新的离线阶段报告。当前授权仍禁止真实连接/输入、M5、旧打包和自动提交推送。

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
