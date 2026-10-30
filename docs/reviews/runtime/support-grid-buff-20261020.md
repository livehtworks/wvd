# 爱丽丝友方选人停滞与重复恢复

本报告保留2026-10-20的排查与验证过程；下文运行、监控和未提交状态均为当时记录。当前操作边界以[当前状态](../../project-status.md)为准，最新循环已取消、监控暂停。

## 已确认原因

同一100轮目标在181ACEA2实例run1至run10保存Completed，加之前18轮共28/100。run11只完成一个业务单元，不计成功。自动巡检发现该轮先后四次在Skill1Try0SelectLevel等待60秒后触发bound_game.restart；这四次是脚本主动恢复，没有据此认定MuMu崩溃。原事件742/1186/1621/2066分别记录62013/61310/60749/61335ms，原帧在diagnostics/1至4.png。

第一张原帧实际为爱丽丝“破若透”的六人友方选择页。combat-select-level没有等级条而返回unavailable，Lv1调用者已正确接到原Support/Confirm等候选；真正失配在support_selection。上排左右角色的状态图标连接卡片轮廓，OpenCV只能提取上排中间及下排三张完整卡片，共4个。旧detect_support_cards先要求至少5个轮廓，而且只允许从上排两个水平轮廓推导布局；本帧两个条件都无法满足，于是返回ambiguous，不取得选人点击资格，最后等待至恢复。

原二进制test_support_selection --buff-frame原帧输出4个candidate并以BUFF_JOINED_CARD_MISSING失败，记录baseline-cards.log。修改后同一原帧得到6个位置，遮掉一张卡片仍拒绝布局，记录fixed-cards.log。原公共等级流程与用户战斗方案不需修改。

## 修复与保护

只修改既有support_cards算法：允许同一排两个轮廓和另一排同列轮廓作为布局依据，水平依据可在上排或下排。三枚不共线锚点可确定网格，但每个缺失轮廓仍必须分别通过已有四条真实边缘覆盖检查；唯一布局、尺寸相似性、边缘阈值和重复布局拒绝条件保持。

页面搜索范围、技能等级阈值、等待期限及恢复规则未修改。没有固定六个点击坐标、没有按模糊选人页自动普攻/Auto、没有给未知或被遮住的卡片授权。复用原prepared_actor、scene、target及输入门禁。

实际原帧保存到tests/fixtures/support-grid，登记哈希和来源；新增六个目标位置的生产Service/角色准备/原Support动作/输入门禁验证，只有设备送达由隔离后端接收。既有support测试增加单独友方原帧入口，复用六位置、遮挡、平移、空白反例；原双友方/敌方入口及其断言保持。

验证过程中曾给既有“敌方单体选择”入口传入柚奈全体技能确认页，得到ENEMY_FALSE_SUPPORT。证据显示该页没有选人X，present及absent均NoHit/unknown，原断言所需的单体选择前提不成立；不将它认定为本修复产生的敌方误识别，不改弱旧断言。原输出support-service.log保留。

## 当前验收和现场

recognition模块首轮在独立support-contracts-261020-c1运行，既有技能/打开/单角色保护/宝箱/恢复/公会回归已通过，新原帧测试在准备角色处失败，未部署。原因是测试直接在详情页执行要求战斗菜单的Prepare，生产确认门禁正确拒绝；已补同一run/generation、事件651/663对应的原菜单frame33，改为原Prepare、公共打开输入及确认、友方目标门禁的真实顺序，没有放宽生产门禁。补诊断代码曾误用OperationResult.code而编译失败，已按实际detail字段修复，原构建日志保留。原帧几何、真实Service六个卡片中心、隐藏/平移/空白反例单独通过；最终模块回执、正式构建、打包、部署和实机续跑尚待完成。

定向support-targeted-261020-c2已按两张原图完成6个位置验证：上排(180/454/728,1200)，下排(182/454/728,1378)。每例均为原菜单准备、原公共打开输入并确认、原Support的scene/target及门禁，隔离后端只收到打开和选人2次输入，enemy(absent)均NoHit。没有虚构施放回执。最终recognition全模块正在support-contracts-261020-c2运行。

最终support-contracts-261020-c2的recognition模块22个入口全部通过，complete=true、device_actions=false。产品输入64742a3af1fa8776c161dbae2f7b2902c5f5f188dd2de886df4042a71e1808bb/1321，与正式构建打包相同；模块入口数量不是游戏轮数。

代理为修复重复主动恢复而停止run11。终态UserStopped/STOP_REQUESTED、result_saved/quiescent=true、成功仍28。停止时ReconnectBoot_Title有一份Sent且delivery_unknown=false的原输入，全部13字段和原结果保存在next/.local/retained-evidence/inspection-issues/index.json的maintenance_stop及正式RunStore原结果；保留原账，不伪造确认或删除。

取证已于2026-10-31收束：next/.local/retained-evidence/inspection-issues/index.json保留完整停点pending及原生结果/日志路径和SHA；baseline/fixed日志与当前tests/fixtures/support-grid原帧承接回归证据，其余重复日志/帧和脚本移入D盘trash。先前整链修复保留，profile/workflows未改，未commit/push。内存归因仍RESOURCE_UNRESOLVED。

## 部署与续跑

原build.py受控构建、package_functional冻结/打包及manage_service Validate/Deploy均完成。候选next/.local/support-candidate-261020，EXE d2a178aa799507581549db5b4f425b7d5bc2895124a8d66294d6f337bb7b8b58，输入64742a3af1fa8776c161dbae2f7b2902c5f5f188dd2de886df4042a71e1808bb/1321。正式新实例3E95E158-0C27-4DE9-AE02-B735D8C17F22/PID50432/start_filetime134369149255476461，原端口17654及data-root保持。部署前核对原维护停止身份及27份配置/流程哈希一致，未操作游戏/模拟器重启或调整驱动/VPN。

续跑请求giant-72-support-grid-20261020-4d4c3c5bcd2443b39a45dccbaac28ac9/GiantBounty/zh-Hant/repeat_count72已实际Running/run1，从网络重试63确认推进到战斗，取得持续新帧及后续角色成功记录。新账目resume-ledger.json及completed-audit.json只算原28加新repeat.completed_cycles，旧run11不计；原末尾Sent标题输入原账保留，不伪确认。初查preparing/run0仅历史准备状态。

新run1自然越过同条件原故障：选级186→Support195→接受198→结果confirmed202→RecordSuccess214→combat216的skill_confirmed/action_confirmed/consumed均true。选级进入至接受输入827.8851ms，没有等待60秒；截至该证据的新段observation.recovery事件0，后续继续其它角色。接受前原帧live-alice-before.png仍仅有相同4个完整轮廓，原帧检测及隐藏卡片反例通过，见live-card-layout.log。这证明同条件实机选人及技能成功路径已推进，不是只accepted，也不是完成了新一轮或100轮。live-alice-support-proof.json与对应事件、resume-confirmation.json保存身份、时间、原帧哈希及真实进展；当时累计仍28/100。

每10分钟监看已更新当前根请求、实例、累计基数及新证据目录。27份正式profile/workflow再次核对未变；本次新修复未commit/push，RESOURCE_UNRESOLVED及MuMu内部崩溃未归因的边界保持。

## 同一100目标完成

2026-10-20 12:08巡检确认本段run1–72全部保存Completed；原5+13+10份与本段72份共100份逐份核三业务单元、result_saved、quiescent，无重复计入segments。旧181ACEA2/run11半轮及其它失败/历史目标排除。最终run72 Completed/busy=false，repeat inactive/state=completed/completed_cycles=72/target_cycles=72，无未决输入、次要或存储错误。

本段72轮执行事件核recovery为0，27份正式profile/workflow哈希保持。这不抹去前段故障，不等于全部历史100轮连续无故障，亦不证明MuMu渲染器内因或RESOURCE_UNRESOLVED解决。wvd定时监看已暂停，没有追加批次、重启设备或自动提交推送。最终审计原件已移至next/.local/retained-evidence/inspection-issues/old100-completed-audit.json，保存100份路径/哈希及当时权威核对；2026-10-31逐份重核正式结果通过，重复current/result副本已收尾。初次内联审计因PowerShell将@($null).Count计为1而拒绝空pending，实际API为null；独立final-audit.ps1显式过滤null后重核通过，未改生产或放宽成功断言。
