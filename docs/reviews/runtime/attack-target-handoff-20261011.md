# 普攻复用单体选敌

## 已确认的现场

新100轮run1、实例E93F3276-872F-446C-8774-2D5DB7CDD157，事件3872/3874点击攻击[66,1200]；3878输入后置confirmed，3880移除pending。画面仍是柚奈ACTIVE、攻击按钮黄色、敌人NEXT。后续没有AttackMarker/AttackArea输入；3970连续异常61308ms后主动bound_game.restart。随后出现ADB_TRANSPORT_FAILED/DEVICE_INSTANCE_STARTING，不倒推此前是游戏自然闪退或官方无响应bug。

代理已停止循环，UserStopped/STOP_REQUESTED/quiescent、成功0/100。原帧在本run diagnostics/1.png，SHA256038aafe25dae6c5c1f0df0ed43b71908c98a0ce06a5c42cc848e83af5b3e3349；日志/原失败原因保持，不将主动停止的新原因覆盖历史异常。

## 代码问题与修复候选

- 独立AttackMarker只用combatTarget，未复用单体流程NEXT/形状/区域尝试。原普攻scene中的support_selection(absent)仅在详情/关闭按钮同时命中后分类；普通攻击没有详情框，结果为unknown，不能命中absent。先前“只读反证不授予输入资格”的解释不准确，已纠正；此失败叶子仍须真实Service原帧证明。
- 撤出本轮新加的独立AttackMarker/AttackArea，改调用原combat-select-target。共享skill_target生成独立敌方坐标及输入资格，NEXT/标记/安全区/24次空间尝试只有一份实现。
- attack_mode参数只区分是否需要技能详情页，默认false保留单体技能语义；普攻要求同一已准备角色、正常战斗指令页、无详情/OK/友方选择。公共入口优先当前目标动作，失配才交接；输入/死亡/未知送达保护不变。
- 普攻目标点击后观察100ms，不开启Auto、不用500ms双击。实际行动推进才记录attack_fallback_confirmed，不消费未施放技能。
- 同步作者验证、资源闭包、公共定义；正式保存版本仍须备份、原revision CAS精确迁移，不能只改包内定义。

## 验收状态

- at1/at2由代理中止，分别为修正详情页条件与补全新增真实原帧夹具入口，未宣称complete；at3的49个app/recognition/runtime入口complete=true，输入ce428d74bbae1684626965d3be44c8f7db62c85a031190a3af087d69e6d001f8。
- 真实原帧的生产Service、NativeOperations准备角色和NativeInputGate/隔离后端组合已通过。at-real-261011/results.json确认旧scene/target为NoHit、友方分类unknown；新scene/target为Hit，target.action_eligible=true，点击(369,281)在隔离后端Accepted一次。不把这个输入资格/送达证明冒充游戏攻击或角色推进成功。
- 以正式profile只读副本核对巨人四种默认覆盖组合，原生图均3024节点且本地化/快捷战斗/等待/返城及业务回执结构通过；日志at-giant-profile-261011.log，不连接设备。
- 正式保存combat-select-target已备份、原revision CAS迁移并逐字段回读，用户参数、布局与原500ms默认技能延时保留，普攻调用独立传100ms。新revision69aa772bf2df4cd03f46ac051ddbffaf7351227e508b541fd8c2651024a1e01b，回执at-save-261011/receipt.json。
- 原build.py/package_functional.py及manage_service Validate/Deploy完成。输入ce428d74bbae1684626965d3be44c8f7db62c85a031190a3af087d69e6d001f8/1299项与验收一致，EXE2d707c605e68383bdc11f86b38f0fd74648cda095b275fb52e1e2da843428bfc。实例0364D50B-F3AB-4790-92B3-76ED27162C39/PID12560/start_filetime134361716761599893，原17654/data-root不变。
- 续跑请求giant-100-shared-target-20261011-eda98023979441e996642952a3d61fdc已实际Running/run1/0成功，从Boot_Attention推进到Boot_Title并有新帧，VPN CONNECTED。旧失败尝试0成功保留、不重复累计，当前仍是同一新100目标；不把accepted当通过。实机无SP时的实际伤害/角色推进仍待自然场景日志，没有拿原帧隔离送达冒充。
- 内存仍RESOURCE_UNRESOLVED；不新增堆采集、外部守护或自动commit/push。
