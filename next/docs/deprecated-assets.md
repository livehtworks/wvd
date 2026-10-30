# 废弃素材与待确认清单

## 当前保留决定与历史盘点

以下数量为2026-10-26历史盘点；本轮仅核对重复隔离夹具，未重做全包判废。当前可见相同图片仍可能承担不同用例，不能按重复SHA删除测试源。最近处置见[资料管理](../../docs/material-retention.md)。

历史复核基线为 `179cfd851877cba2f2df401d86a905ff9ae088f6`。本页登记素材的已知引用、替代证据与未确认事项。2026-10-26执行C03已修正24条来源元数据并重算manifest revision；没有删除、搬移图片或改变PNG字节。具体推进、验收及停止条件见 [收束计划](../../docs/maintenance.md)，来源事实见 [收束盘点](../../docs/reviews/engineering/consolidation-inventory-20261026.md)，资产和正式配置职责见 [持久数据职责](../../docs/data-authority.md)。

| 当前核查项 | 已确认事实 | 整理结论 |
| --- | --- | --- |
| 包成员 | manifest 共 593 个成员，其中 PNG 591 张 | 旧 508 张计数不再代表当前全包 |
| 语义配方 | 148 项语义资源直接引用 176 个不同模板名 | 需将当前配方引用纳入索引 |
| `source_refs` 为空或缺失 | 274 条空数组、21 条缺字段，共 295 张 PNG | 不据此判废 |
| 上项中的有效反例 | 38 张仍被当前语义模板直接引用：17 条空数组、21 条缺字段 | 这 38 张不能按旧空索引移出包 |
| 来源路径错误 | 24 条已按精确映射改为 `next/resources/images/…`；实际源大小/SHA与包成员一致 | 已修元数据；593成员、591PNG的path/hash/bytes和aliases不变 |
| Next 源中未入包的头像原图 | `monster-portraits/cyclops.png`、`dark_enemy_candidate.png`、`red_hair_serpent.png`、`scorpion.png` | 用户头像、嵌入配置和来源用途未核清，继续保留 |

当前有效引用的反例包括 `character_panel_zh_hant`、`combat_skill_confirm_zh_hant`、`guild_report_ready_zh_hant`、`network_retry_zh_hant`、`recovery_panel_zh_hant`、`shared_close_cross`。空数组与缺字段都不足以证明运行时未使用；也不能仅以相同 SHA、名称相近或未入包为依据去除来源。

`inn_icon`、`guild_icon`、`edge_of_town_icon`、`ruins_icon` 的形状模板替代关系仍保留为候选证据，尚未证明它们已从旧工程、用户扩展、manifest 和测试的全部引用中退出。`supportSkillCheck.png` 已退出 Next 旧识别流程，但基线中的 `src/script.py` 仍有调用，继续保留源图与包副本；不通过删除旧功能或弱化测试来完成整理。

当前[逐图引用证据](../../docs/reviews/evidence/asset-reference-closure-20261026.json)已覆盖591张包内PNG：记录语义配方、原生/流程/测试/旧Python的精确字面量样例、语言分类、旧空/缺索引及4张头像原图的当前配置嵌入比对。统计复核为295=274空数组+21缺字段，38=17+21；字面量扫描不能证明动态名称、用户扩展或历史来源全部退出，因此没有把无命中图片改成废弃。隔离小夹具实际执行`sync_authoring_resources`，已证明正确Next来源可恢复缺失成员，源字节变化仍拒绝。本页不授权永久删除，原图用途未证实退出时继续保留。

## 历史索引记录（含 2026-10-03 补充）

以下保留旧扫描的计数、候选表、逐项列表与补充日期，标题层级作归类调整。原文中的“当前”“本次”指该历史扫描范围，不表示 2026-10-26 的全量结果；本轮未用旧列表执行清理。

本清单按 `next/packs/wvd/manifest.json` 当前索引生成；只登记，不删除、不移出构建。
`source_refs` 是静态引用索引，动态路径只覆盖清单列出的有界候选；无引用不等于运行时一定不用。
每次改动入口或资源清单后重新核对，不把旧结果当作永久删除授权。

本次索引图片 508 张：已替换旧模板 4 张；有静态/动态/别名引用 293 张；待确认 211 张。

### 已由新版入口替换

2026-10-03补充：`image/supportSkillCheck.png`已由`support_selection`边框布局识别替换，退出Next原生战斗与公共选敌流程；原图包含变化数值，当前友方实帧仅0.4349。旧Python仍可能使用，文件不删除；历史发布快照不改写。以下计数属于原扫描快照，不是本次重新全量索引。

以下四张在当前 Next 语义资源、原生城市探针及已检查的动态候选中无有效引用；旧工程 `src/` 是否另行使用，以旧工程自己的调用链为准。文件仍保留。

| 旧素材 | 当前替代与依据 |
| --- | --- |
| `image/inn_icon.png` | city.inn.entry 已改用 inn_icon_shape.png |
| `image/guild_icon.png` | city.guild.entry 已改用 guild_icon_shape.png |
| `image/edge_of_town_icon.png` | city.edge.entry 已改用 edge_of_town_icon_shape.png |
| `image/ruins_icon.png` | city.ruins.entry 及原生跳轮入口已改用 ruins_icon_shape.png |

### 待确认：无静态引用（210）

这些文件不能仅凭清单标成废弃；迁移或清理前需核查旧工程动态拼接、配置/mod 引用和实际运行入口。
例如 `image/Economy.png` 仍由内置英文旅店流程使用，索引中的空引用并不等于源码未使用。

- `image/7000G/icantagreewithU.png`
- `image/7000G/illgo.png`
- `image/7000G/illgonow.png`
- `image/7000G/leavethechild.png`
- `image/7000G/noeasytask.png`
- `image/7000G/royalcapital.png`
- `image/7000G/why.png`
- `image/7thDist.png`
- `image/AssembleParty.png`
- `image/B2FTemple.png`
- `image/B4FLabyrinth.png`
- `image/B5FWarpedOnesNest.png`
- `image/COS/ArnasPast.png`
- `image/COS/COSENT.png`
- `image/COS/EnaTheAdventurer.png`
- `image/COS/Okay.png`
- `image/COS/b2fentrance.png`
- `image/COS/requestwasfor.png`
- `image/COS/takehimwithyou.png`
- `image/COS/替换-凯旋.png`
- `image/CSC.png`
- `image/City_DHI.png`
- `image/City_fortress.png`
- `image/DH-R5.png`
- `image/DH-entrance.png`
- `image/DOF_quit.png`
- `image/DOL_quit.png`
- `image/DOLtarget1.png`
- `image/DOLtarget2.png`
- `image/Economy.png`
- `image/Economy_替换.png`
- `image/Edit.png`
- `image/FFXI/City_VNH.png`
- `image/FFXI/FFXIStone.png`
- `image/FFXI/diggable.png`
- `image/FFXI/entrance.png`
- `image/FFXI/nothingToDig3.png`
- `image/FortressArrival.png`
- `image/LBC.png`
- `image/LBC/EnaWasSaved.png`
- `image/LBC/request.png`
- `image/LBC/symbolofalliance.png`
- `image/LBC_quit.png`
- `image/PartyManagement.png`
- `image/PartyManagementTitle.png`
- `image/SC/SC.png`
- `image/SC/SCB1F.png`
- `image/SC/SCB2F.png`
- `image/SH_B2F.png`
- `image/SH_cave.png`
- `image/SSC/Leap.png`
- `image/SSC/SSC1F_left_1_once.png`
- `image/SSC/SSC1F_left_2_once.png`
- `image/SSC/SSC1F_left_3_once.png`
- `image/SSC/SSC1F_left_once.png`
- `image/SSC/SSC1F_right_once.png`
- `image/SSC/dotdotdot.png`
- `image/SSC/goldenchest_1.png`
- `image/SSC/shadow.png`
- `image/SSC/trapdeactived.png`
- `image/TWC/TWC.png`
- `image/TWC/TWCB1F.png`
- `image/autoBattleEnable.png`
- `image/bag.png`
- `image/beginningAbyss.png`
- `image/blackScreen.png`
- `image/bounty/Slayhim.png`
- `image/bounty/cuthimdown.png`
- `image/bounty/lethimgo.png`
- `image/caveinZone.png`
- `image/chestStone.png`
- `image/closePartyInfo.png`
- `image/closePartyInfo_fortress.png`
- `image/closePartyInfo_waterway.png`
- `image/combatAuto.png`
- `image/combatAuto_2.png`
- `image/combatTarget.png`
- `image/cropped.png`
- `image/cursedWheelTapLeft.png`
- `image/cursewheel.png`
- `image/cursor.png`
- `image/darklight.png`
- `image/darklight_lightIt.png`
- `image/donothing.png`
- `image/fastforward.png`
- `image/fishing/FISHBait.png`
- `image/fishing/baitbox.png`
- `image/fishing/iconbait.png`
- `image/fishing/quit.png`
- `image/fishing/startfishing.png`
- `image/flee.png`
- `image/fordraig/AskDagger.png`
- `image/fordraig/B2Fentrance.png`
- `image/fordraig/B2Fquit.png`
- `image/fordraig/B3F_teleport.png`
- `image/fordraig/B3fentrance.png`
- `image/fordraig/B4F.png`
- `image/fordraig/B4Fquit.png`
- `image/fordraig/Entrance.png`
- `image/fordraig/FirstBoss.png`
- `image/fordraig/FirstTrap.png`
- `image/fordraig/InsertTheDagger.png`
- `image/fordraig/Leap.png`
- `image/fordraig/RequestAccept.png`
- `image/fordraig/SecondBoss.png`
- `image/fordraig/SecondTrap.png`
- `image/fordraig/TryPushingIt.png`
- `image/fordraig/b1fquit.png`
- `image/fordraig/readytoBoss.png`
- `image/fordraig/thedagger.png`
- `image/fordraig/thirdMach.png`
- `image/fortressb3f.png`
- `image/fortressb7f.png`
- `image/gaint_candelabra_1.png`
- `image/gaint_candelabra_2.png`
- `image/give.png`
- `image/gradeexam.png`
- `image/gradeup.png`
- `image/guild.png`
- `image/leaveDung.png`
- `image/lounge.png`
- `image/marker.png`
- `image/markspot.png`
- `image/next.png`
- `image/noneed.png`
- `image/notenough_close.png`
- `image/output.png`
- `image/press!!!.png`
- `image/quit.png`
- `image/ready.png`
- `image/requestToRescueTheDuke.png`
- `image/rowFlag.png`
- `image/ruins.png`
- `image/sandman/sandman_1.png`
- `image/sandman/sandman_2.png`
- `image/sandman/sandman_bondmate.png`
- `image/specialRequest.png`
- `image/spellskill/char/0 面具.png`
- `image/spellskill/char/F 伊亚玛斯.png`
- `image/spellskill/char/F 凛音.png`
- `image/spellskill/char/F 凛音_sp.png`
- `image/spellskill/char/F 吉拉德.png`
- `image/spellskill/char/F 吉拉德_sp.png`
- `image/spellskill/char/F 基利昂.png`
- `image/spellskill/char/F 夏莉莉妮雅.png`
- `image/spellskill/char/F 奥尔德里科.png`
- `image/spellskill/char/F 尤尔萨.png`
- `image/spellskill/char/F 影狼.png`
- `image/spellskill/char/F 普修利.png`
- `image/spellskill/char/F 柚奈壬姬.png`
- `image/spellskill/char/F 狮樱.png`
- `image/spellskill/char/F 艾妮琪.png`
- `image/spellskill/char/F 艾尔文.png`
- `image/spellskill/char/F 莉娜莉亚.png`
- `image/spellskill/char/F 莉瓦娜.png`
- `image/spellskill/char/F 莉瓦娜_sp.png`
- `image/spellskill/char/F 萨维亚.png`
- `image/spellskill/char/F 萨维亚_sp.png`
- `image/spellskill/char/F 贝卡南.png`
- `image/spellskill/char/F 贝卡南_sp.png`
- `image/spellskill/char/F 赛德.png`
- `image/spellskill/char/F 迦鲁巴多斯.png`
- `image/spellskill/char/F 银莲.png`
- `image/spellskill/char/F 阿尔博里斯.png`
- `image/spellskill/char/F 雅蓓妮斯.png`
- `image/spellskill/char/F 雅蓓妮斯_alt.png`
- `image/spellskill/char/F 雅蓓妮斯_sp.png`
- `image/spellskill/char/G 亚沙.png`
- `image/spellskill/char/G 亚沙_sp.png`
- `image/spellskill/char/G 克拉丽莎.png`
- `image/spellskill/char/G 克洛艾.png`
- `image/spellskill/char/G 克洛艾_sp.png`
- `image/spellskill/char/G 切羽.png`
- `image/spellskill/char/G 加斯顿.png`
- `image/spellskill/char/G 卡米尔.png`
- `image/spellskill/char/G 奥利芙.png`
- `image/spellskill/char/G 奥菲莉亚.png`
- `image/spellskill/char/G 巴克什.png`
- `image/spellskill/char/G 本杰明.png`
- `image/spellskill/char/G 海因里科.png`
- `image/spellskill/char/G 玛丽安娜.png`
- `image/spellskill/char/G 甘道夫.png`
- `image/spellskill/char/G 米拉娜.png`
- `image/spellskill/char/G 米拉娜_sp.png`
- `image/spellskill/char/G 艾莉赛.png`
- `image/spellskill/char/G 芙尔特.png`
- `image/spellskill/char/G 芙尔特_sp.png`
- `image/spellskill/char/G 阿米莉娅.png`
- `image/spellskill/char/N 人类忍者.png`
- `image/spellskill/char/N 无名人类女僧侣.png`
- `image/spellskill/char/N 无名兽人女盗贼.png`
- `image/spellskill/char/N 无名妖精女法师.png`
- `image/spellskill/char/P 亚当.png`
- `image/spellskill/char/P 亚当_sp.png`
- `image/spellskill/char/P 叶卡捷琳娜.png`
- `image/spellskill/char/P 叶卡捷琳娜_alt.png`
- `image/spellskill/char/P 叶卡捷琳娜_sp.png`
- `image/spellskill/char/P 哲鲁夫_alt.png`
- `image/spellskill/char/P 拉纳维尔.png`
- `image/spellskill/char/P 拉纳维尔_alt.png`
- `image/spellskill/char/P 拉纳维尔_sp.png`
- `image/spellskill/char/P 爱丽丝.png`
- `image/spellskill/char/P 爱丽丝_alt.png`
- `image/spellskill/char/P 爱丽丝_sp.png`
- `image/spellskill/char/P 黛波拉.png`
- `image/spellskill/char/P 黛波拉_alt.png`
- `image/spellskill/char/P 黛波拉_sp.png`
- `image/transfer.png`
- `image/whowillyougiveitto.png`
- `image/wraped.png`

### 待确认：显式未分类（1）

这些文件不能仅凭清单标成废弃；迁移或清理前需核查旧工程动态拼接、配置/mod 引用和实际运行入口。

- `image/boot_attention_zh.png`

### 使用边界

- 不自动清理以上素材；确认退役必须先证明没有生产入口、配置或 mod 使用。
- 新增或恢复引用时，必须从待确认列表移除并同步资源清单。
