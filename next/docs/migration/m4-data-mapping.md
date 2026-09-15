# M4 配置与任务数据映射

固定旧源：`6585f4075f5714ab522aa582993860c09af912c1`。本轮不 import 旧 Python，
不使用旧 GUI，也不向生产 `config.json` 或 mod 写入。当前已有离线数据和运行状态，无完整游戏任务入口。

## 配置

`prepare_m4.py` 从固定 Git 的 CONFIG_VAR_LIST 生成 33 字段描述、来源行号、默认字面量与 schema；
gettext 只读取 msgid，不执行翻译函数。解析器为 `LegacyConfigImporter`，保存者为 `ProfileStore`。

| 旧数据 | 当前处理 | 未承接部分 |
| --- | --- | --- |
| GENERAL、DEFAULT、任务名称区段 | GENERAL 后合并被选区段；启用任务专用且目标区段存在才选该任务，否则 DEFAULT；只为缺字段应用已核实默认值；冻结值供离线图与状态工厂消费 | M5 GUI 编辑和生产运行入口未开放 |
| EMU_PATH / EMU_INDEX / ADB_ADRESS | 保留拼写和值，包括允许的 null；不改成本机路径 | 真实设备配置应用 |
| SKIP_COMBAT_RECOVER / SKIP_CHEST_RECOVER | 保存原“跳过”语义；WvdRunState在遭遇结束时形成恢复请求，dungeon_route/dark_light调用实际恢复子图 | 所有任务组合与现场质量未验 |
| STRATEGY | 保留字段和顺序；策略来源、成功消费、头像/技能/等级/目标与Auto动作已接入离线链 | 全任务策略组合及真实场景质量未验 |
| role_var / skill_var / target_var / freq_var / skill_lvl | 战斗执行消费角色、技能、目标、等级；固定旧script.py没有freq_var读取，新版同样保留但不发明频次语义 | 当前子流程证据不能代表全部任务/资源组合 |
| TASK_POINT_STRATEGY | 保留overall_strategy/task_point字符串步骤键；已连接路线确认后的步骤推进及策略切换 | 专项各局部路线/恢复交接仍需逐项验收 |
| LANGUAGE / KARMA_ADJUST | 保留旧键及有符号字符串；善恶选择确认后通过KarmaCommitPort/ProfileStore只对新版profile执行CAS原子更新，失败不重发副作用 | 所有任务组合未验；旧config永不写回 |
| 未知值 | 完整 legacy_document 原树保留，legacy_passthrough 按 JSON Pointer 分类 | 不自动成为新版运行参数 |

`import_copy` 的目的目录必须原本不存在，先复制再解析，并比对源 hash；原文件不修改。
兼容导出未改变字段保持原树，改变值写回原来源区段；原来缺失的字段按已核实类别确定区段。
新版 profile 保存必须具备全部 33 个值；不是用默认值悄悄补齐一份缺字段的 CAS 草稿。
CAS 以内容 revision 和独占 sidecar 锁防止旧稿覆盖，冲突不重载再覆盖。文件安装失败不能报告成功。

## 任务与 mod

`legacy-quests.json` 是固定 Git 的原始字节副本。WvdQuestCatalog 保留所有原字段和数组，
58 条均与固定 Git 独立对照，包括 43 个 dungeon 和 15 个 quest。
WvdTaskPlan 进一步解析顺序 fallback、目标提示与回城参数，见 [计划数据验证](../m4-plan-validation.md)。
PipelineCompiler/publish_workflow与真实Maa离线入口已实现，43个普通任务的入本/路线/有限
迭代图可编译；专项逐项推进。仍没有58项完整任务执行成功证据，也未开放生产入口。

任务 mod 按给定文件顺序追加。冲突名称反复增加 `_mod`，中文标题反复增加 `_自定义`，
中英任务名缺项按旧规则互补，错误条目留下诊断。基线条目不被覆盖。
图片顺序为基线原名、显式alias、mod原名/alias；AssetResolver和publish_workflow通过封存
副本发布图片mod及来源证据，不在运行中扫描用户目录。新目录模板先由准备阶段生成
派生revision，再冻结Run；活动资源仍不接受目录引用。

## 运行状态

版本化状态工厂和 WvdRunState 已实现。以下按组索引范围；逐字段实现、重置事件与尚未接入的消费者
以 [状态生命周期报告](../m4-state-validation.md) 为准，不能把整组视为已完成业务。

| 原字段组 | 后续必须核对的语义 |
| --- | --- |
| _LAPTIME、_TOTALTIME、_COUNTERDUNG、_COUNTERCOMBAT、_COUNTERCHEST | 正常有限段延续、统计口径、新 Run 隔离 |
| _TIME_COMBAT、_TIME_COMBAT_TOTAL、_TIME_CHEST、_TIME_CHEST_TOTAL | 起止时间和累计时间，改为单调时间后注明口径差异 |
| CURRENT_STRATEGY、TASK_STEP_INDEX、_ACTIVESPELLSEQUENCE | 来源选择、深复制、成功消费、任务步切换，不能每帧重填策略 |
| _MEET_CHEST_OR_COMBAT、_RECOVERAFTERREZ、NEED_RECOVER_WHEN_BEGINNING、_SUICIDE | 恢复触发、死亡处理、入场恢复 |
| _COMBATSPD、_ZOOMWORLDMAP、_RESUMEAVAILABLE、_BYPASSAFTERRESTART | 同一 Run 可延续状态与重启清除的区别 |
| _CRASHCOUNTER、_MAXRETRYLIMIT、_LAST_BAGCLEAR、_IMPORTANTINFO | 恢复预算、清包周期和展示状态，不改变用户“背包不处理”的运行现场 |
| _RUNNING_EMU_PID、_SKIP_SCREENSHOT_WARNING、_LAST_FOCUS_CHECK、_PAUSE_CHECK_UNTIL、_LAST_DEBUG_IMAGE_AT | 设备代次和瞬时观测，不能沿用旧帧/旧 PID |

完整分母见 `m4-implementation-map.json` 与 `m4-task-status.json`。
其中规划 owner 不代表函数已存在；未实现条目的实际入口保持 null，不生成空 handler。
