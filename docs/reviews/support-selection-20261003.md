# 友方技能选位修复

## 现场与问题

用户确认：技能详情下方独立的两行三列队友卡片是友方选择界面；单体和横排均点配置中的六个位置之一，不做技能名称分类。原图保存在`next/.local/combat-support-capture/support-20261003-004547.png`及`support-20261003-004845.png`。

旧数值模板supportSkillCheck得分0.4349。另发现明细位于(822,687)，超出旧搜索带上界750；旧close模板包含英文Close，繁中截图得分0.791，不能继续按完整英文文字判定关闭控件。

## 唯一生产链

- `support_cards.hpp`只负责几何：灰度、Canny(20/60)、3×3闭操作修补细小断边、矩形轮廓尺寸/填充度检查、内外双边去重、两行三列且尺寸间距一致的唯一布局。使用OpenCV现有算子，不引入OCR或新依赖。[官方边缘检测文档](https://docs.opencv.org/4.12.0/dd/d1a/group__imgproc__feature.html)。
- `support_selection`负责上下文：当前语言的明细＋语言共用关闭叉号。明细搜索带扩至[720,620,180,560]；叉号取旧素材[18,12,40,40]，不含英文Close，阈值0.8不变。卡片区域由实际明细下缘、关闭叉号上缘与屏幕下半区共同约束，不用六个固定小ROI。
- 测量结果区分present、absent、ambiguous、unknown。唯一完整布局才授权友方选位；只有详情控件完整且无卡片候选才允许进入敌方检查，仍须无确认按钮、角色和敌方目标成立。候选不完整或不规则时不点任一目标，保持当前流程的新帧观察与既有异常恢复，不伪装识别成功。
- 6个配置位置映射为检测卡片的中心；按上排左中右、下排左中右排序。没有固定坐标回退。小断边由闭操作修补，大面积遮挡不猜第六张卡的位置。
- 同帧相同上下文只提取一次轮廓，缓存仅保存有界JSON坐标/诊断，不保留灰度图；换帧由既有Service清理。诊断含state、candidate_count、cards、roi和card_detection_ms。
- 战斗Support分支、原生skill_target的敌我排除和公共combat-select-target全部使用新判定；发布依赖同步，不再加载旧数值模板。旧素材仅保留历史用途，不删除旧Python资源。

## 直接验证

`test_support_selection`调用真实生产Service和正式公共流程库，不连接设备。两张友方实帧命中六张卡，六个目标中心正确；已有敌方扇形射击实帧无选择卡片，友方NoHit且允许进一步敌方识别。合成遮一张卡为ambiguous、不得授权双方目标；合成卡片整体平移仍定位新中心；黑帧无详情不授权。完整战斗图能编译，确认Support为识别中心点击且不存在旧模板引用。

最终验证日志`next/.local/support-selection-test.log`；明细证据`next/.local/support-check-168202457721400/evidence.json`。首次卡片测量362.57ms，后续实帧4.72ms，敌方1.94ms、两张合成退化/位移约3.60/4.36ms；首次初始化开销未归因，不把暖态耗时冒充首次耗时。该结果证明识别与输入参数接线，不代表已实际释放技能。

## 部署与数据

部署已完成：candidate71已运行，正式服务原帧探针确认友方present=Hit、右下选位=Hit、absent=NoHit；敌方present=NoHit、absent=Hit。保存流程revision为`a0859655d9ea4139aa05114005e52dd6f8079b4a601b9e5afe77fa0add1e1469`，唯一更改`target.scene.conditions[2]`，完整旧文件保存在`next/.local/support-workflow-backup-20261003-011318`。服务Idle、run_id=0，profile SHA256未变。此段是部署后核对记录，不修改已构建二进制。

候选71继续使用17654和原data目录，不运行游戏任务。当前保存的combat-select-target带已有本地修改，因此不能整份接受新内置文件：只经正式PUT/CAS替换target.scene中的旧supportSkillCheck反证，保存完整API与作者原文备份，并核对其它字段不变。记录分别为`next/.local/support-deploy.log`、`support-workflow-update.log`和`support-deployment-check.json`，以成功核对为部署完成依据。

回滚：停服务，保留当前流程，人工从support-workflow-backup目录恢复对应旧作者定义，再部署candidate70；profile、游戏进程和历史运行数据不变，不自动切回旧判定。
