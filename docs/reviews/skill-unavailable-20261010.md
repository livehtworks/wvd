# 技能不可用仍重复点击

## 现场与根因

- 服务C0D9BDD2-9B82-410B-8ADF-CB777CE3460A的run2，用户报告不可用技能仍反复点击。已通过正式停止接口停循环，回读UserStopped、busy=false、quiescent=true、repeat.active=false。当前批次实际完成1/100，不是100轮通过。
- run2诊断9.png（frame1718）显示柚奈四格技能均灰掉、SP为2/239；诊断1.png（frame1392）显示面具技能也灰掉。旧measure_skill_availability要求另一格至少100个亮像素，实际四格bright_pixels都是0，因此即使文字边缘存在仍返回NoHit。
- 原公共combat-open-detail在同角色原菜单每1500ms补点，结果窗口20000ms且未限制提交次数。run2五次no_progress均为11次提交、20.53至21.36秒后转防御；不是网络恢复或整轮次数限制。
- 旧src/script.py:2673起，打不开详情尝试3次、每次1秒，然后退出技能路径。旧Auto降级不恢复，因为此前用户已要求不得因技能失败开启持续Auto；沿当前手动防御语义。

## 修复

- 用指令栏仍亮着的文字作参照，不要求其它技能亮着，支持四格同时禁用；空白文字、整页变暗不作为禁用证据。外层仍须同角色/技能菜单两帧命中才防御；识别不授予点击坐标。
- 公共入口同页补点最多3次，保留5秒迟到结果观察。原页打不开才走既有UnavailableExit；转场先查结果、不继续旧坐标，未知送达/网络恢复规则未改。
- 源码及发布包同步；正式保存定义仅修改menu_retry_max_submissions=3和postcondition_timeout_ms=5000，保留其它用户编辑。备份next/.local/architecture-audit-20261010/skill-evidence/combat-open-detail.before.json；CAS前revision=fe51d25411888217eb96ee09bf01553d0be061fa39b4dc4c075a3521a58f0556，回读revision=ec7267042458665c50cfe87eefab2de6e38a0cece179a6f3e46bb16d37ea1f5d。

## 针对性验证

- skill-real-frames.log：真实柚奈/面具全灰截图四格均Hit；真实爱丽丝正常技能截图四格NoHit；对应空白/整体暗化反例及Service结果均通过。图片复制到独立验证目录，未修改原图，未操作游戏。
- skill-open-contract.log：公共定义/生产执行器同页补点、转场不重发、失败交接防御、不点Auto通过。受控端口仅隔离场景与输入，无假实机结论。
- 构建日志skill-target-build.log；错误工作目录导致旧EXE验证未运行，不作旧版本重现实证。
- 原始实机证据：next/.local/c11-flow-product/data/runs/C0D9BDD2-9B82-410B-8ADF-CB777CE3460A/2（execution-events.jsonl、action-timing.jsonl、diagnostics/1.png与9.png）。不把修复后的实机循环或长期稳定性记为通过。

## 部署

- skill-product-build.log及skill-service-deploy.log均exit0。source_commit仍fd6b617c9425bea3379e32d7b5a7812fff3b8d9c，产品输入hash=d47e781df9115fe0c82a0cd46ae979ca2d906c8536658a06f49ea40bb0c3f0df，1289项，含未提交修复。
- 服务2D62AB2D-9AA3-4444-8676-A418B4118111、pid28288，原17654端口及data-root；实际回读Idle、busy=false、quiescent=true，未新开循环。正式combat-open-detail回读revision与上述CAS一致，max_submissions=3、timeout_ms=5000。
- profile SHA256仍3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C；游戏、模拟器及VPN没有被本轮关闭，没有改用户技能方案或消费宝石。

## 恢复循环与实机

- 用户再次要求继续后，恢复原100轮剩余99轮。首个恢复请求42442851-4dec-4bb9-93eb-7832fa7c72df以combat.unowned_skill_detail失败，run1未完成任何轮；这是暂停时遗留的爱丽丝技能详情，不隐去失败记录。只手动点击关闭该未施放详情，未使用Auto或改方案。自动处理暂停遗留详情仍未闭合。
- 随后批次da45c56d-e380-40ad-a00c-389845d90a29，run2真实Running、repeat.active=true、0/99；承接原批次1轮完成，不等同连续100轮验证。
- 新run2 action-timing.jsonl实测Skill0Try0Disabled=0.91秒、DisabledAgain=0.41秒，随后直接点击UnavailableDefend坐标513,1200，确认后进入下一角色普修利；没有在该面具回合调用combat-open-detail。检查时累计11个输入、Auto坐标850,1100尝试0次，其它角色4次正常技能入口。该证据证明本次自然灰色场景已修复，不推导为全部战斗场景或长期循环稳定。
