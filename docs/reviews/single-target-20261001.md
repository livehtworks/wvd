# 单体技能链排查与修复

## 现场与原因

- 正式实例 `A5282D1A-20DD-433C-8758-4CEBCD341A0A` 的 run25 停在 `Skill1Auto_Fallback`，原因为 `INPUT_RESULT_UNCONFIRMED:FLOW_STAGE_TIMEOUT`。其截图是繁中技能详情，不是已确认的自动战斗。
- 配置已准备 `0 面具`、左下技能、1级；日志先连续执行三次 `combat-open-detail`，未进入 `combat-select-target`，随后在 `(850,1100)` 发出自动保底点击。现有单体目标步骤并非没有编写，而是被错误的详情判定挡住。
- `combat.skill.detail` 的 ROI `[720,750,180,300]` 截止到 y=1050，现场明細模板位于 `(809,1004)`，尺寸46×80，底部到1084，不能完整落入旧ROI。旧范围模板分数0.3403；扩展到 `[720,750,180,430]` 后0.9677，阈值0.82不变。
- 同一失败帧的白色目标箭头得分0.9137，达到原0.86阈值。NEXT文字没有匹配，不阻碍既有箭头候选。本次未降低购买保护或战斗识别阈值。

## 修改与验证

- 修改唯一作者源的英文/繁中详情控件带，并同步生成资源包、CMake冻结目录和产品产物。菜单、选级、选敌、详情关闭和自动保底通过已有共享语义资源生效，未另设识别旁路。
- 用户保存的 `combat-select-target` 也通过正式PUT/CAS修正其旧友方目标检查ROI为 `[580,1350,320,250]`，只修改该字段；其余参数、边和位置保持。保存前后快照在 `.local/single-target-20261001/target-workflow-before.json` 和 `target-workflow-after.json`。原revision `8233272c9e0acedca9528d970e6592ae1759fe33430a3246eaed9cc51b602364`，新revision `e9c8dfd9b0e78fc99b734d4d50b1d48f423180743b2da9e737eb34284bac1943`。
- 正式识别Service对真实失败帧确认：详情Hit、确认按钮NoHit、箭头Hit、当前角色Hit；真实王城帧详情NoHit。仅执行这五项有限检查，未运行全量矩阵。它们不能代替实机技能选敌与施放的验证。
- `automationd`、`test_native_author` Release构建成功；候选 `candidate42` 的EXE SHA256为 `ac0f993c8366cf4d610f5ee5ff265c0ade814694ad4353ed4767c8a9f6aec4dc`。

## 运行边界

- 旧candidate41服务PID32448的run25已静止并落盘，循环inactive，设备控制已正式断开；游戏、模拟器和VPN未关闭。
- 原100轮目标确认完成26轮：上一实例2轮，本实例24轮。不计入失败run25，不混入更早批次；恢复时余74轮。
- 用户再次明确要求自行完成后台处理后，已核对身份并结束旧服务，部署至原17654端口。当前为candidate46、PID2556，EXE `8eda232766e16f2b23119e7f8183739009ac35ab418cab21a26c6b8ff3842c0d`。实例 `AD8B2855-D8C9-461E-A84F-63E644DFF893`，74轮请求 `39b88315-c571-4a19-bdad-c05efb72816f` 的run1已经Completed、3/3业务段、quiescent=true、result_saved=true；报告余量0，本轮住宿200G提交1次且inn_rest_completed=true。循环已计入1/74并自动续下一轮，原100轮总目标累计27轮，余73轮。配置revision保持不变、AUTO_START_CLASH=true。
- 恢复现场另发现任务冷启动只接受城市/公会/郊外，战斗详情的“關閉”又可能命中公会展示关闭。现由悬赏入口复用正式副本已定义的战斗、宝箱和复活子流程，再返城查看报告；公会关闭明确排除战斗。没有把旧战斗补记为已完成循环。
- candidate42续跑入口缺口、candidate43重复战斗图触发节点上限、candidate44漏接chest出口均保留失败记录。最终复用已有定义并补齐出口，未提高图节点上限。
- candidate45的实例`B83E48B7-0EA4-4FA4-A946-73E2DF056644` run1实机确认两次`combat-select-target`，点位(592,553)/(651,552)，分别630.1/642.6ms取得confirmed，随后进入Skill1/Skill2的RecordSuccess；另有Skill17全体技能成功，战斗结束进入Battle_Dungeon。随后返哈肯遇到宝箱，旧返城函数把encounter出口接到通用失败，不能把该轮算作完整成功。candidate46改为交还遭遇处理，再回到原业务阶段；不重放已经提交的报告或补记循环成功。
- 历史独立作者流程 `live-scorpion-combat-20260925` 本轮尝试在准备阶段报 `json.exception.invalid_iterator.214`，没有执行；未将该独立入口算作通过。当前蝎女正式任务入口不受此错误阻断，该历史入口仍待另查。
- 证据目录 `.local/single-target-20261001/`：`current.png`、`cases.json`、`recognition.log`、`build.log`、`stage.log`。没有commit/push，保留其他既有未提交修改与用户配置。
