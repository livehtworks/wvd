# 巨人战斗停滞检测与恢复

## 现场与核账

本轮目标仍为 `giant-200-20261030-a81556d0ff4449fea1fd1294c2779065` 的200个完整成功轮次。续段 `giant-200-resume-20261030-c07755bb5c1e41ee9062d6df4a785cd9` 的 run2–20 已逐份核对：Completed、三业务单元、details_complete、result_saved、quiescent，单轮结算1、待交账0、无交账/住宿未决；共19成功，剩181。run1与run21均为失败半轮，不计成功。

run21与此前run1相同，最后停在 `Task_FirstDungeon_Battle_Actor_AnimationWait`，父调用最终报 `FLOW_INVOCATION_TIMEOUT:Task_FirstDungeon_Entry`。正式实例E6C10EF7…保持，非服务重启或存储限额触发。

## 已确认原因

操作菜单消失时，原图将其识别为正向战斗中的正常动画等待，并持续重认菜单。问题在等待续期：`region_changed`比较行动条区域的原始灰度像素，变化超过2%即作为进展；头像发光和未来行动队列也会变化。

对失败且静止现场的两张新鲜无输入帧，当前角色及队伍数值未变化，正式Service却给出 `Hit / changed_fraction=0.06818103448275863`。FlowExecutor据此反复刷新60秒局部期限，未走 `AnimationStalled`；最后由父调用总期限结束整轮。因此反复启动不能解决检测误判。游戏内技能停滞的触发根因尚未定位。

第一版a500修复的两帧离线回归通过，但实机验收失败：同一战斗仍无菜单、零输入，队伍卡片金色高亮脉动进入数值采样边缘，产生0.9874306相似度并误续期。验证续段`giant-200-stallfix-20261030-6c86f2c27cab437d94925d46279ac3c5`由代理在确认零输入后显式停止，UserStopped/0单元/saved/quiescent，0成功；没有重启游戏，剩余仍181。不能将该第一版列为修复成功。真实Service红回归见`hud-red.log`，现场比较见`live-sample-analysis.json`，停止回执SHA见`first-deploy-validation-stopped.json`。

预分析的最后动作帧带有技能横幅，未拿它证明稳定停滞。正式红/绿回归使用独立采集的idle帧对，原始字节及SHA保留在 `next/tests/fixtures/combat-progress/cases.json`。

## 修复边界

- 唯一执行链和原轮询不变：每次选路仍重认战斗结束、操作菜单及阻挡事件。菜单回来即回到Prepare，不等满60秒，不点击Auto试探。
- 当前原生战斗消费者统一使用 `combat_progress / combat_stalled`。读取当前角色头像及六名队员HP/MP/SP区域的七份归一化特征；数值范围排除卡片边框，并用9×9局部背景去除保留文字笔画，避免高亮脉动。未来队列、头像边框和背景不取得进展权限，不放宽原相似度阈值。不增加OCR或外部守护器。
- 新鲜采样间隔1秒，真实角色/数值变化才能续期；原60秒无进展及15秒采样缺口规则保持。同一旧帧不靠时间流逝取得进展或静止证据；局部读取范围仍受Scope约束。
- 无进展仍沿原 `AnimationStalled → combat.auto_progress_timeout → recovery_policy`，由原协调器执行游戏StopApplication/StartApplication及原Boot/同任务恢复。不修改模拟器、VPN、总任务期限或空间配额，不清业务账、不重放Unknown输入。
- 导航与既有通用区域探针保持原契约；正式profile/workflows和公共流程定义不修改。

## 验证

证据根：`next/.local/giant-200-20261030/stall-fix/`。

| 验证 | 结果与证据 |
| --- | --- |
| 修复前真实帧复现 | 原区域误报及第一版HUD误报均预期失败，`red-reproduction.log`、`hud-red.log` |
| 正式工厂+真实Service+固定帧 | 两种待机及强卡片高亮误进展被拒，真实角色/队伍数值变化接受，限制Scope越界拒绝，四种原始样本交替连续60秒无进展命中；最终`hud-green.log`、`hud-green/combat-progress.json` |
| 原工厂+FlowExecutor | 正常短动画等待、菜单重现、真实进展续期、无进展的原恢复原因与游戏重启计划、战斗结束出口；`actor-safety-final.log`、`test-flow/combat-animation-*.json` |
| 菜单与策略回归 | 原菜单输入修复、单角色自动策略、停止与低意志力保护；`actor-safety-final.log` |
| 输入与恢复门禁 | 原轮询分派、Unknown人工停止、祖先未决输入、重启前诊断及身份；`flow-safety.log`、`coordinator-safety.log` |
| 第二版原工厂+FlowExecutor | 原短动画、无进展恢复原因与计划、真实进展续期、菜单恢复选路；`hud-flow.log`、`hud-flow/` |
| 正式停滞现场 | 原session持续62.3246秒后以`combat.auto_progress_timeout`结束；seq191原恢复计划，seq196/200确认StopApplication/StartApplication且skipped=false，EnsureVpn跳过且原实例2仍运行。菜单恢复进入Prepare，随后战斗输入确认；`acceptance2/automatic-recovery-proof.json` |
| 完整业务与自动续轮 | 新实例run1为Completed/3单元/details_complete/result_saved/quiescent，交账及住宿清账，SHA `c173f14331872da4bb13bf7b87b20865c07849d79fa21fb7bbcb0ed0724fb9ca`。run2自动Running，已完成1单元、17次输入确认/0拒绝，后续新战斗输入继续确认；`acceptance2/verified-new-successes.json`、`running-confirmation.json`、`final-live-progress.json` |

中间测试构建遇到C++20的string/JSON比较歧义，已改为显式字符串读取；一项恢复测试错误比较了自然增长的elapsed计时，已改为逐项核对真实未决账目、确认、策略和unit，未改变生产验收或禁用用例。原失败日志保留。

最小固定素材为三张PNG及两张原始JPEG，共5,045,598逻辑字节；两张本轮新采集帧直接移入素材目录，仅复制一张既有对照帧和两张复现高亮的生产recent-frame字节，没有保存完整data-root或模型夹具。

## 发布与后续

用户要求先修复、部署，再重启游戏继续。已使用原build.py的有界构建函数和begin/finish回执，前端源码及冻结产物逐项相同后复用；核实Failed/静止后经原manage_service停止、原package_functional打包、Validate和Deploy。正式历史不迁移，线上CI及监控不启动。

最终第二版正式EXE为`15670137f3229039d088363af5580cf72665857b3a0a0af767f955c96811fa75`，产品输入`fc734057b1a2f39358a8609930afa9df75546b83fd80abfb2aa3e5aaf93eb72c`/1354、619成员，构建时基线为2f70858及修复工作区，构建先于本次源码提交。匹配PDB为GUID92997ab5-efb2-4e86-a59a-520d07dca97c/Age4。正式实例`7FFEDDFD-AFD6-469B-95A8-14D4ED7122B0`，原data-root/17654保持；41配置、19份成功和旧失败/中止回执SHA复核一致。旧49ad候选在`next/dist/.wvd-next-native.transactions/previous-34ef51bf629d45f7b7aed5d8ab98aba1`，匹配Age2符号保留；显式回滚步骤及发布回执见`stall-fix/release2/deploy-plan.json`、`deployed-identity.json`、`symbol-pair.json`。

第一版验证段明确停止并排除后，第二版剩181成功轮只提交一次，续段根`giant-200-stallfix2-20261030-39faac202a3b485aa4e18ffa455e91dc`，关系已先落盘到原目标账本和`acceptance2/plan.json`。旧失败半轮保持，不合并旧目标或重复启动。部署验收时核累计20/200、剩180；首个新完整成功与自动下一轮真实输入均通过，循环继续。修复部署阶段没有commit/push，不启动监控或线上CI；游戏自身停滞触发原因未定位，本轮修复的是程序检测与原生恢复。

本次按用户后续授权核对修复、测试素材及文档并提交推送`fork/codex/t0-lifecycle-repair-20261025`，排除`.vscode`。5帧素材SHA及900×1600尺寸核对一致，合计5,045,598B；源码产品输入仍为上述fc734057…3eb72c/1354，复用最终真实Service60秒、FlowExecutor及实机恢复/完整业务验收，不重跑或重建。只读核对截至2026-10-30T21:49:21+08:00已逐份核新5成功，连同旧19共24/200、剩176，当前run5为Completed。个人仓库Actions总开关false、两工作流disabled_manually；本次不操作服务或游戏，不另开循环。回执在`stall-fix/commit-review/`。

6份专用小标记包共53,436B已同盘移到`D:/trash/wvd-combat-stall-tests-20261030`，回执及恢复清单保留。第一版失败验收的a500中间候选621文件/203,932,383B及匹配PDB147,714,048B，核交付成员SHA、无活动进程引用、发布事务锁和回滚候选保护后，整体移至`D:/trash/wvd-combat-stall-intermediate-20261030`；不保留为当前构建或回滚候选，原路径已退出。历史符号回执的a500路径由`release2/intermediate-retirement.json`映射至trash，恢复清单在两端保留。当前1567/Age4与旧49ad/Age2保留。均无永久删除、同盘迁移释放空间0；正式数据没有迁移或清理。
