# M4 任务类型化数据验证

日期：2026-09-14。这是 M4.3 的数据解析部分，不是完整 Pipeline 编译交付。
原 `WvdQuestCatalog` 保留目录；新增 `WvdTaskPlan::parse` 由实际 `wvd_m4_check` 的
`inspect_plans` 选项调用，不连接 Controller，不生成假成功任务入口。

## 已实现

- 58 项逐条保留 TaskID、类型及完整原始 JSON；43 个 dungeon 的路线与入本顺序可类型化读取。
- 15 个 quest 保留自己的声明和源字段，显式 requires_special_case=true；没有冒充已迁移代码分支。
- `_EOT` 保留严格顺序、间隔、末步骤标志，区分普通定位点击、EVENT 和 intoWorldMap。
- fallback 区分模板、坐标、滑动和嵌套顺序列表；不是 Maa 候选 next，不执行 shell 字符串。
- 只接受旧数据的 `input swipe` 四整数语法，未知命令、附加命令、非法坐标等直接报错。
- `_TARGETINFOLIST` 按 TargetInfo 旧 setter 解释：方向名展开、默认六步扫描、position/stair 坐标、
  harken/Bharken 楼梯参照和普通 ROI 分开；chest 遮罩按旧顺序追加，不擅自去重。
- `_RTT` 保留目标、滑动和默认 `[550,1]` dismiss 参数；preEOTcheck、FloorCheck 单列。
- 未消费的特殊对话/回调等字段留在 source，不丢弃，也不谎称已经绑定行为。

来源为固定旧源 `6585f407` 的 TargetInfo（139 行）、LoadQuest（189 行）、StateEoT（2472 行）、
TeleportFromCityToWorldLocation（1935 行）、TeleportFromDungeonToCity（1973 行）及原 quest.json。
严格类型/边界校验是计划发布前的拒绝条件；不会把坏输入降级成默认坐标后继续运行。

## 验证

本节保留前轮数据解析的验证记录；当前构建另见 [流程报告](m4-workflow-validation.md)。前轮构建 `m4-state-final-build`；构建身份
`96c9b74c48c61c39ffc1b250b78acbb7bcbfa2bdc8e31c08952e4f6a39f0e5c5`。
本轮 M4 总计 17 组通过，其中 8 组数据、6 组状态、3 组任务类型化解析。

任务证据为 `next/.local/m4-plan-33qfcq96/`：

| 用例 | 断言 |
| --- | --- |
| all | 原生解析全部 58 项，对照独立 git show 原始字段、入本/目标顺序及末步标志；IWO 和 FFXI 回城参数核对 |
| hints | 坐标和楼梯参照不能当矩形；默认扫描含首次不滑动；chest/default 叠加排除区保持旧语义；未知原树字段不丢 |
| shell / verb / coords / roi / direction / empty / interval | 非法命令、未知操作、越界坐标/ROI、未知方向、空地下城路线及非正间隔拒绝 |

所有用例读取自有临时任务文件，前后 hash 不变，真实连接/输入为零。
同构建的数据证据 `m4-data-9k5_p23l`，状态证据 `m4-state-tests-18qm7y86`；
日志统一在 `next/.local/m4-state-31d6be223ee5454daa24b726aad9c20c/`。

## 未完成

## 当前专项静态检查与编译成本

正式源码 `f83a747`，日志 `m4-special-plan.log`，私有根 `m4-plan-nphew16v`：
八方法138.495秒，七通过、一ERROR。全部43入本、43普通迭代、58原树及两个已实现专项
绑定通过；43路线批次触发测试30秒外部看护。它没有进入设备运行，也不是Session超时。
专项检查仍列出其余十三项，不隐藏分母；负REST_INTERVEL明确拒绝。

定位到常量识别模式的隐式资源依赖在每个节点反复展开。修正一仅在一次资源收集内去重
常量模式展开，显式图片及依赖参数的技能等级/楼梯目标仍逐项收集；独立validate仍重新
收集并检查完整资源集合。不延长测试看护或修改运行预算，待构建后复验及产物逐图比对。

## 剩余边界

已有有限子流程 PipelineCompiler 和封存发布，入城/住宿/Auto 实际执行另见流程报告。
任务解析结果仍固定 pipeline_available=false，因为全任务编译尚未完成，不能用局部入口替代全部 58 项。
完整任务的动作绑定、资源闭合、路线及原生回跳编译仍未齐备。
这些不能由“源树保留”证明通过；M4.3 仍为 PARTIAL，全部任务 release_allowed=false。
专项代码里的动态路线、对话回调、交任务/跳周目等还须逐 case 承接，不能由该解析器推导出来。
下一实现边界是完整任务图与其余业务动作，不应重复解析测试冒充进一步迁移。
