# 当前阶段优先与无资源普攻保底

## 本轮边界

用户要求无SP不能默认防御，开箱不能先等待其它流程的识别，并将当前阶段优先规则核对到其它流程。新100轮请求 `giant-100-phone-reset-20261011-1ed434f3056546eba0cbc2e03740ecb4` 的run3已停止、quiescent，完成0轮。不要按小时巡检再次启动旧候选。

游戏侧彻底无响应卡死仍由用户处理，本轮不为该事件部署额外探测动作。只撤回本代理尚未部署的导航探测源码与其测试；既有返哈肯同次无进展等待保留，用户修改不撤销。

## 已查实

- FlowExecutor::select_next按候选声明顺序选中即返回；Exception/Special仅在业务候选/已知等待未解释现场后进入。并非执行器每次把所有事件跑完。显式网络/Pause/下载/死亡覆盖层属于输入保护，不以优化名义移除。
- 工厂的候选顺序和复合谓词仍能反转该规则：开箱Entry先Reward/Combat/Revive/Ambush/Done才Begin；选择、拆锁公共入口又先展开跨流程advanced。正常开箱阶段漏接再起OCR排除锚点。
- 本轮原action-timing显示开箱公共entry约2418ms、disarm约2904ms、await约2451ms；取图约12ms，recognition_other约1527ms/段，OCR调用3/4/4次。这是各段计数，不能直接当成独立模型推理次数。
- 重分类当前未发送动作时，即使另一个Input候选scene为NoHit，仍查询其target；它影响所有业务分支。现在scene不成立就不查询该候选的按钮，scene Error仍错误，输入资格及pending不放宽。
- 不可用技能原来统一指向UnavailableDefend。六个人持续缺SP时可能全部防御、不造成伤害。现在改手动攻击指令与敌方目标尝试，逐帧约束同一角色，实际行动推进才结算保底；未施放技能条目保留，不开启持续Auto。

## 调用链核对

| 位置 | 核对结果与处理 |
| --- | --- |
| 根循环 | Application持有批次，未增加外部守护/执行器；恢复只计实际成功回执 |
| 执行器 | 首个候选命中后停止选路；统一修scene失败仍查询target的问题 |
| 战斗 | 已有combat_phase优先当前菜单/详情，保持；修改无资源普攻保底及真实结算/日志 |
| 开箱 | 新chest_phase先当前阶段，再按失配展开声明的交接；公共选人/拆锁与原生工厂统一 |
| 补给 | 原生Entry及输入后继改优先ResumeAttempt/Begin/Ready/Recovered，Encounter只作交接；既有supply_context、死亡保护及尽力尝试合同不变 |
| 悬赏公会 | 繁中CheckReports已有PrepareReport优先，NoMoreReports在列表加载且无报告跨帧确认后才成立；不更改提交合同 |
| 住宿 | 已有付款账目、房型、离店按阶段推进，特殊剧情出现才处理；不回退到全局认页/等待背包补充 |
| 跳轮/入本 | 已按当前章节/目标和步骤后继推进；启动/恢复入口的收敛候选用于未知当前位置，不等同每步全局重扫 |
| 地图/导航/返城 | 到达/无路线与当前移动需独立证明；遭遇是可能随时出现的正常交接，不能移除反证导致误点。当前原有模态与输入保护保留，待针对性验证后的成本证据决定进一步调整 |
| 救人/再起 | 既有独立职责保留，正常开箱可排除再起OCR，但死亡反证仍优先；多人死亡人工停止不改变 |

通用all/any的错误传播不改。阶段专用识别只在已命名的当前阶段内采取顺序求值，全部可达图片和隐式OCR仍在编译/发布时冻结校验；缺素材不能作为运行NoHit放行。

## 验收状态

- 最新实机反例优先：run1事件3872/3874点击Skill13UnavailableAttack的[66,1200]，3878输入结果confirmed，3880 pending移除；后续未进入AttackMarker/AttackArea选敌输入，3970在61308ms后主动bound_game.restart。原帧diagnostics/1.png显示柚奈仍为ACTIVE、攻击按钮点亮及敌人NEXT。循环已停止0/100。尚不能从这些事实确定具体失败叶子；此前受控六角色夹具并未覆盖这一实际选目标画面，不能冒充实机普攻闭环通过。

- 源码候选已通过定向验证、正式构建/冻结包校验并部署，保存定义已迁移；不将部署与100轮成功混为一件事。
- cp1新增重分类夹具缺Await观察节点，被生产程序验证拒绝；cp2补节点后暴露unguarded Input选路饿死后继。cp3已修全局输入资格选择：先scene，scene成立才查target；工厂动作不再用all(scene,target)作为选路guard。cp3运行时与普通攻击相关入口通过。
- cp3实际Service的supply-plan入口退出0xC00000FD。对象反汇编确认evaluate_uncached单次栈帧0xCDA0（52640字节），新阶段配方继续递归进入这一大函数存在栈成本风险。已把开箱阶段与再起配方分派移到小函数，Windows禁止重新内联，不提高栈容量或结构深度。cp4提取函数遗漏常量导致编译失败，cp5修正后通过；不将此候选栈问题当成历史内存增长归因。[异常码说明](https://learn.microsoft.com/windows-hardware/drivers/debugger/debugging-a-stack-overflow)、[栈探测说明](https://learn.microsoft.com/en-us/windows/win32/devnotes/-win32-__chkstk)。
- 定向app/recognition/runtime的49个入口在 `next/.local/cp5-261011/receipt.json` complete=true；输入hash为1a5304e017c3594daa391b3eb45eb4fa79b3a3c85f00d336e3116b9f1dabd010。包含六角色无SP生产图/执行器、原有Auto边界、策略不消费、公共页面交接与scene-first重分类断言，不是49轮游戏。
- 实际Service的历史开箱原帧与当前奖励原帧分别通过，输出cp-opening-261011/cp-reward-261011。奖励、handoff/post为Hit；active/disarm/opening为NoHit，六个独立帧身份请求合计1232.82ms、33次实际模板比对、OCR调用0。该值不是单次输入耗时或实机整轮提升。
- 从正式库只读复制20份公共定义/profile，在隔离副本精确调整两份宝箱定义后，生产整链八种受控场景通过、pending=0，日志cp-giant-saved-261011.log。该入口未自动建立证据目录；创建目录前的前七种JSON轨迹未落盘，保留完整通过日志和末种轨迹，不宣称八份轨迹完整。设备和视觉叶子受控，不是实机八轮。
- 停止前开箱历史帧 `next/.local/c11-flow-product/data/recent-frames/20261011T031618510_r3_f971.png` 可用于真实Service验证，不能称为当前现场。用户指出现已开完箱，经只读Sky及现有device/capture重新确认当前是奖励页；新原帧在next/.local/cp1-261011/current-reward.png。现场没有拆锁或奖励点击，不制造死亡或额外消费。
- 正式两份保存定义已备份、原revision CAS迁移并精确回读，坐标、延时、布局、其它参数及profile哈希保持。迁移回执next/.local/cp-save-261011/result.json，choose新revision0410d7e0576e5447565f08434eb3a4c398b6b368f4b262ddceba6ce6c26ad6b6、disarm新revision236f9d29f29c683a6ffda6d421b07ff267a7695b5a52072082154d37a65a1745。构建资源工具6项通过，cp-resource-tools-261011.log保留故障注入的预期超时与最终OK；不是全tools模块矩阵。
- 原build.py/package_functional.py正式构建与manage_service Validate/Deploy完成，1299项输入hash与cp5一致；EXE080d3d6b5917276d71ef1ece75772813799256c1b4422e1fcb15e6d0414e8abe，正式实例E93F3276-872F-446C-8774-2D5DB7CDD157/PID16764/start_filetime134361673322405857。端口17654、data-root不变，原服务按身份关闭、模拟器保留。
- 当前请求giant-100-phase-priority-20261011-1368d3e0e97c4a858350ffd3827b1b86已实际Running/run1/0成功，继承本新100目标；旧22和修复前run3的0成功尝试均排除。最新新帧从奖励继续进入返城、公会、跳轮并到Task_FirstDungeon_Route0_Moving，VPN CONNECTED。账目在原launch-ledger，小时巡检指针已更新，不增加外部守护。仍需自然掉箱后的输入→恢复耗时及无SP实机普攻证据，不拿受控夹具冒充现场。
- 内存仍RESOURCE_UNRESOLVED，没有新增ETL/堆采集；没有commit/push，保留既有未提交候选及用户修改。
