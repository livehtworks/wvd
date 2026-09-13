# 业务与配置迁移约束

本表把源码层的完整索引补充为可审查的业务链，不替代 `feature_inventory.json` 的逐项记录。
所有目标归属均为待实现；本轮没有执行这些游戏行为。

## 生命周期与恢复

| 基线入口 | 必须承接的链路 | 迁移责任 / 验收族 |
| --- | --- | --- |
| main.AppController.check_queue | start、stop、finished、转 7000G、下载及重启事件各自分支 | runtime + api / M2-LIFECYCLE，M6-UPDATE |
| run_farm_safely / finishingcallback | 工作线程异常也要回传完成，按钮状态不能替代真实退出 | runtime / M2-LIFECYCLE |
| HeadlessActive / parse_args | 显式配置路径、无界面启动、停止反馈；M1 新服务暂不支持旧 CLI | app / M5-CLI |
| Farm / DungeonFarm / QuestFarm | dungeon 通用链和 quest 专项分支分开；任务 ID 不丢失 | games/wvd/tasks / M4-TASKS |
| restartGame / WaitGameBootReady | 游戏关闭、连接恢复、VPN、启动页、下载与进入可识别页 | recovery + devices / M4-RECOVERY |
| CheckAndRecoverDevice / ResetDevice | 限定实例身份，写回新设备对象，使旧句柄/坐标失效 | devices / M3-DEVICE |
| GameFrozenCheck / TryResumePauseOverlay | 结合状态与动作反馈，保留诊断，不只看全屏动不动 | recovery / M4-RECOVERY |

真实任务分发位于 `script.py:3471` 的 match/case，消息分发位于 `main.py:71`。
索引同时登记 if 和每个 case 的 subject、pattern、guard、顺序及出口，不能只数 if 分支。

## 视觉与战斗

| 基线能力 | 必须承接的语义 | 目标 |
| --- | --- | --- |
| 截图与 NormalizeScreenshotImage | 900x1600 基准、真实后端、错误与重连，不能凭旋转猜通用适配 | devices / ViewportProfile |
| _check / CutRoI / CheckIf / CheckHow | 原匹配法、阈值、中心、排除 ROI、越界处理、调试不污染原帧 | vision / WvdLegacyRecognizer |
| 同帧缓存、模板与亮度 mask 缓存 | 帧和资源 revision 改变时失效，不能沿用旧观察点击 | Session 范围缓存 |
| NEXT / 三角标记 / 低置信度兜底 | 识别与目标动作拆开；每次动作后的状态确认与退出详情不能丢 | vision + combat |
| CheckRolePortraitMatch / 技能等级 | 头像裁剪与遮挡，角色/技能/目标配置保持对应 | vision + combat |
| Pause 布局、反证和可选 OCR | 不将角色/技能页面当 Pause；OCR 缺失与 NoHit/Error 分开 | vision / WvdPauseRecognizer |
| ReloadStrategy / StateCombat | 条目消费、任一即完成、全局与任务点重置条件，不能每帧重新补满 | combat / WvdCombatState |
| 自动战斗退路 | 先处理详情/确认覆盖，再确认 Auto；不能盲点后一直等 | combat / WvdCombatRecovery |

## 导航与专项任务

- `StateMap_FindSwipeClick`、`StateMapSearch`、`StateMoving_CheckStop`：地图点、楼层、是否到达、automove 未消失的反馈要组成完整移动链。
- `PressWorldMapTargetArea`、`WaitWorldMapTargetEntered`、两类 Teleport：入城确认后立即结束大地图输入，不能继续点剩余候选坐标。这是导航业务，不是底层 Press。
- `StateInn`、`reunionParty`：选队伍不等于住宿，补镐子必须由住宿业务确认；普通间隔、王室套房和强制补给不能合并丢失。
- `StateChest`、`PressDisarmSafely`：箱子/陷阱/复活/转战斗边界独立，转场停止旧输入。
- `StateEoT`、`_EOT`、`_preEOTcheck`、`_RTT`：保留原动作数组顺序和等待/重试参数，不机械改成 next 候选节点。
- 蝎女悬赏、跳周目、沙人缘、挖矿、近端/远端钓鱼等全部按原 case 和任务字段登记，不套成同一刷图模板。本轮未领任务、交任务、抽卡、卖装备或执行任何游戏输入。

## 配置权威与字段

`CONFIG_VAR_LIST` 的 33 项是顶层配置；源码字典字段另外登记，包含策略组、技能行、
GUI 临时行数据与任务点选择。不能把这些字典字段全部当成用户持久化字段。

| 配置内容 | 原语义 / 必须保留 |
| --- | --- |
| GENERAL / DEFAULT / 任务 ID 区段 | LoadConfig 先读取 GENERAL；启用 TASK_SPECIFIC_CONFIG 且对应任务存在时选任务区段，否则选 DEFAULT；最后任务区段覆盖合并值 |
| GUI save_config | 按配置表分类；保留其他任务区段，当前通用项写 GENERAL；当前模板项只写选中任务或 DEFAULT |
| EMU_PATH / EMU_INDEX / ADB_ADRESS | 保留旧拼写与值，但不能把源码默认地址当本机探测结果；导入写入权威统一在 storage |
| STRATEGY | group_name、skill_settings、complete_one_as_all；每行 role_var、skill_var、target_var、freq_var、skill_lvl |
| TASK_POINT_STRATEGY | 实际键为 overall_strategy、task_point；task_point 使用步骤序号字符串索引，不擅自改名为 overall |
| SKIP_COMBAT_RECOVER / SKIP_CHEST_RECOVER | 保存值仍表示跳过；UI 正向显示转换只在映射层完成 |
| KARMA_ADJUST | 保存时普通数字补正号，业务确认后的持久化不能丢失 |
| LANGUAGE / 策略默认名 | 原 gettext msgid 在索引中原样保留；不执行翻译，不静默改业务键 |
| 未知键 / 历史运行字段 | 正式导入时保留未知值并分类报告；不根据 GUI 的重建式保存逻辑删除用户未知键 |

`LoadSettingFromDict` 与 GUI 读写共同构成导入语义的证据，不能直接实例化 FarmConfig
来生成默认值。盘点只解析配置表，避免类级 locals/翻译/文件日志副作用。

## 资源、扩展、诊断和更新

- `utils._build_quest_data`：基表优先，mod 校验类型/名称并补中英文名，冲突任务追加 `_mod`，中文名追加 `_自定义`。不读取用户 mod 也不能因此退役扩展能力。
- `LoadTemplateImage` 先 resources，找不到再 mod；不能颠倒优先级。大小写差异单列，不批量转小写或重编码 PNG。
- 日志队列、摘要框、异常 traceback、截图限频和保留策略由 storage + Vue 运行页承接；不能在 GUI 回调里直接落盘/清日志。
- 更新检查、下载进度/取消、校验/解压/重启全部有原函数与分支索引。新版将只接受新版制品，本轮未移植旧自动更新器。
- 旧 bat 的环境隔离、配置保护、资源收集与运行目录保留仍生效。本轮构建只输出 next/build、next/web/dist，不覆盖旧 dist/wvd。
