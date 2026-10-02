# 战斗目标与频次修复

## 用户要求与实际语义

- 目标只保留左上、中上、右上、左下、中下、右下角色，新行缺省左上。删除低生命值、默认、不可用；它们之前没有对应的生命值择优实现。
- 目标仅影响友方单体选择；敌方保持NEXT/三角标记及既有Auto保底，全体技能、普通防御不按队友位置选敌。没有把左上队友当作敌方攻击坐标。
- 频次只有“用完后移除”和“重复该动作”，存储分别为`用完后移除`和`重复`。过去freq_var只是保留字段，执行器无条件移除；现在由生产CombatStrategy结算。
- 移除是从当前运行的方案副本删行，不删除用户配置；按现有方案重置时机重新恢复。重复是角色下一次行动重新识别后使用同一行，不是连续发点击。
- 同一角色按方案行顺序选择，重复行会阻挡其后同角色行；一次性动作应放在前面。现有“任一完成即结束方案”仍具有最高优先级，会清空整个运行方案，界面已明确标注。
- 失败或仅发起Auto不结算；沿用既有行为，Auto保底经后置确认后也会结算该次动作：一次性行移除、重复行保留。这不表示原技能成功释放，诊断的operation仍区分success和auto_confirmed；新增action_confirmed与真实consumed字段，不能将动作确认误读为删行。

## 配置与边界

### 友方技能当前如何区分

生产入口`combat/turn.cpp`先确认战斗、当前行动角色与技能详情，再选等级；随后按Support、Confirm、Enemy、Missing顺序判断。Support在900×1600右下`[580,1350,320,250]`匹配`supportSkillCheck`，命中才点击配置中的队友位置；Confirm识别当前语言的确认按钮；Enemy要求没有确认按钮、没有Support标志，再定位敌方NEXT/三角目标。防御为配置中的单独分支。它不是技能名称/OCR语义分类，也不是用户选了“左上角色”就把技能当辅助。

`supportSkillCheck.png`沿用旧Python实现，素材实看为暗色背景上的`600/600`、`40/50`数值区域，不是专属“选择队友”提示。该模板的专属性、不同队伍数值/布局下召回，以及友方漏识别后落入敌方分支的风险仍需真实友方技能正反例确认；本次不能据执行器和UI检查宣称此分类已实机可靠。确认按钮也只证明可直接确认，不能仅凭此严格推断一定是全体技能。本次仅记录，不擅自改分类逻辑或点击游戏取样。

ProfileStore为唯一配置写入者。旧profile先检查revision，保留完整原文字节备份，再一次性迁移版本2；覆盖values、default_values、task_overrides、legacy_document中的STRATEGY，不修改其它配置。旧低生命值/不可用/空目标改左上，旧每场/每次启动一次/空频次改用完后移除，原重复保持重复；原来的重置策略不变。未知值明确报错，不猜测。

旧格式导入在既有LegacyConfigImporter入口规范化，运行时不维护两套消费逻辑。备份不是运行数据权威；旧Python config.json、mod、运行历史不回写。回滚须停止新版服务、保留迁移后profile，再人工恢复同revision原文备份并部署旧候选，禁止新旧同时运行。

## 针对性验证

- `test_strategy_frequency`直接调用生产CombatStrategy和ProfileStore：一次性转下一行、重复保留、未确认失败不消费、重复回执拒绝、重置恢复、重复与整组完成优先级。
- 正式profile只读复制到独立目录，核对迁移后整个JSON仅发生约定字段变更、备份字节一致、重复启动幂等、CAS保存重载及正式源哈希不变。
- `strategy-frequency.spec.ts`连接18758隔离数据的真实原生服务，不Mock接口：桌面1440×1000和窄屏390×844验证六目标/两频次、新行默认值、编辑保存与刷新重载。2项通过，禁止任务/设备写接口；测试服务已正常退出。
- 证据：`next/.local/strategy-frequency-native.log`、`next/.local/strategy-frequency-ui-test.log`及`next/web/test-results/strategy-frequency-*/strategy-frequency.png`。原生与Vue生产构建通过。
- 不声称本轮实际战斗验收；没有点击游戏、启动循环或进行全量历史断言。

## 部署核对

candidate69使用既有17654和原data目录；部署/身份/空闲状态及正式配置迁移核对写入`next/.local/strategy-frequency-deploy.log`、`next/.local/strategy-frequency-deployment-check.json`。仅在这些证据完成后认定部署完成。旧candidate68与迁移备份保留供人工恢复。
