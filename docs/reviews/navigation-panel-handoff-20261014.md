# 恢复后导航反复重启

## 原始现场

新100次请求giant-100-20261014-2f75fbcbe4104e8f9c6e4611a4a57f39在实例5D4EEF14的run3开始。run3至run9每轮各一次bound_game.restart，均在Task_FirstDungeon_Route1_Choose等待60646至61398ms后触发CONTINUOUS_EXCEPTION_TIMEOUT。run9事件3350原图为角色强度面板，旧导航输入仍为Sent、delivery_unknown=false。没有把再起、网络恢复重分类或实例启动重复计为游戏重启。

run9关闭面板输入(66,1500)于3257记录confirmed，观察帧320；随后同帧的导航Ready命中，3281点击(840,277)。run11关闭X补试两次后于2523记录confirmed，2547点击(832,287)，该轮没有取得重启事件，不能按前七轮模式推断每一轮必然失败。现存最近帧不能还原这两个关闭结果帧，不能断言它们是假图或某次点击必然导致面板再次打开。原始导航后重新出现面板的具体游戏时序仍有限制，但缺失交接已由代码与run8/9失败原图证实。

用户要求修复后重新开始循环。本轮沿正式stop入口结束run12，累计9个成功留作历史，run12维护停止不计成功。原停点无未决输入、quiescent=true；原图、run9/11/12日志及现存98张最近帧已复制到next/.local/handoff-261014/，不覆盖回执。

## 确认的缺口与修改

1. 导航输入结果只接受迷宫、遭遇、哈肯菜单、无路线提示等状态；角色面板没有出口，所以已变化的页面仍在旧导航Await中等待到60秒。
2. dungeon_route的HealingPanel还要求healing_required=true。一次恢复已结束后，该值为false，即使角色面板出现也不进入现有恢复流程。
3. recover_in_dungeon的Unneeded直接按业务值返回，未关闭已打开的面板。

修复沿原唯一调用链：导航后识别到角色面板则结束旧输入等待，调用既有recover_in_dungeon职责关闭面板，再以明确blocked交接返回原调用者，重新执行同一任务点。此交接不确认到达、不消费任务点，也不刷新静止导航的期限。正常导航候选仍先尝试，当前导航不匹配后才检查面板；没有增加通用全模板扫描。

原恢复流程无治疗需求但已有面板时，只使用原BACK/X关闭动作，不新建治疗请求、不重复Recover；导航、补给、Boot共用已有page_actions::close_character。遇敌继续交既有encounter，Unknown输入仍由原执行器保护。没有更改60秒期限、ROI、阈值、默认技能延时、驱动或VPN。

## 验证范围

扩展既有healing-effect-contract：无需求的角色/恢复面板关闭、恢复结束后导航途中出现面板并重走原任务点、遇敌交接、未知送达禁止关闭。使用生产工厂/编译器/执行器，叶子观察受控。

扩展原recorded-input-contract：用真实迷宫原帧、run8与run9失败原图、关闭后迷宫原帧模拟设备回帧，真实Service/输入门禁/生产图执行；仅设备传输隔离。要求只有导航和原X两个输入、pending结清、无新治疗需求、task_step不前进。原五张恢复/战斗/救人/低意志力回归保留。

构建、模块回归、真实原帧结果、部署及自然实机观察完成后在此补记，不把受控设备验证冒充无重启实机通过。定时检查保持用户关闭状态，未获commit/push授权。

最终hf-c3-261014的app/recognition共31入口complete=true，产品输入ba011bfacb68f22a483d9ed4ff9fa6dd59fe3ff907079174977185dc1088a1ac/1301。hf-c1因新增测试未装配navigation-resume公共库失败；hf-c2因受控FlowPorts默认把未知输入清理当成功，测试走到终点失败。补真实公共库及明确未确认清理后，原Unknown保护断言通过，没有放宽生产保护或删除失败用例。

hf-real-261014/results.json的7个原帧组合通过真实Service/门禁/工厂/执行器。run8/9导航面板各两个输入（原导航、原X），pending=[]、healing_required=false、task_step=0；旧两张角色面板各两个BACK加一个X，战斗/救人/低意志力原图均0输入。动作后的帧由受控后端切换真实原图，这不证明实机游戏按同一时序关闭，也不冒充一轮游戏完成。

## 部署与新批次

原build.py完成正式构建/打包，冻结hf-candidate-261014经原manage_service的Validate/Deploy通过。EXE fdcc6b0ce1edd66c25bc448facfed30b5e2d85321d00d1f155a006dd0b617d76，产品输入与hf-c3相同；仅按身份停止旧空闲5D4EEF14服务，游戏/模拟器保留。

新正式实例E9BDAE77-86A3-43D8-BD25-ED08F3B1EE15/PID36068/start_filetime134364625896589622，端口17654、原data-root不变。40份profile/workflow哈希未变，未修改正式保存定义。新请求giant-100-panel-handoff-20261014-16df40b732224180af41c254a89e883c，目标100、基数0，旧9次及所有历史成功排除；定时检查保持PAUSED。账目next/.local/giant-100-handoff-261014/launch-ledger.json。

已实际Running/run1并有新战斗技能输入、当前战斗结束后公会及新入副本进展，启动证明resume-confirmation.json。自然恢复到导航及首轮是否无重启的结果继续观察后补记，不把accepted或离线通过写成长期稳定通过。

自然首轮已保存Completed、quiescent=true、details_complete=true，回执副本run1-completed.json。返哈肯Route1_Choose接受事件5431到哈肯菜单Retreated事件5445为5910.88ms；整个run1的bound_game.restart调用0。之后返城、住宿/任务收尾完成，已实际Running/run2，累计1/100。该自然轮没有出现导航后重新打开面板，因此不能冒称实机触发并验证了新增交接；该触发仍由两张故障原图组合验证覆盖。首轮无重启也不证明未来100轮均无异常。自然证据natural-navigation-evidence.json及run1-progress-events.jsonl位于新批次目录。
