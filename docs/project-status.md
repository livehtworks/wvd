# 项目当前事实

## 当前产品

- 当前产品在 `next/`，C++20/CMake + Vue + OpenCV + ONNX Runtime；Maa和旧Python链不参与原生构建/运行，历史源码、config、mod、dist不删除。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有循环；没有新增并行执行器、兼容别名或自动回退。
- 配置/流程/回执数据权威见 [数据职责](data-authority.md)，执行注意项见 [操作约束](execution-notes.md)，核心语义见 [核心合同](../next/docs/core-contracts.md)。

## 当前源码候选与现场范围

- 2026-10-20按用户明确要求整理提交与推送：包含自aa79e44以来已实施的连接/恢复衔接、低意志力保护、技能等级识别、爱莉安诺特素材及对应回归和报告；不包含用户独立.vscode配置、正式profile/workflows或.local运行数据。下方“未commit/push”为各次验收时的历史状态，提交和远端结果以Git历史为准。本次提交不重新构建、部署或操作正在运行的循环；正式产物仍由已核对的product_inputs_sha256绑定。

- 2026-10-20巨人整链及恢复衔接修复已构建、打包、预检、部署。修复归还回执收尾、同unit已完成路线保留、住宿结果交回原所有者及三段已记账未返回时的收尾；不新增重启理由、旁路、ROI/阈值调整或清除Unknown保护。candidate4的34处受控恢复、正常整链15场景/2保护反例、app/recognition/runtime共53入口及3个重点执行器复核通过，不是游戏轮数。新候选next/.local/chain-candidate-261020，EXE1e92ed7032ada6c02441711e3719cedb838714f2e42890e33cd06599fb56fdcb，输入b903948e668d5d5c78ed7265a8443aded1378f1cb7f39e94fd1705c4170a06b8/1317；正式181ACEA2-92B3-45CF-AB8B-41920F9DF5B1/PID26652/start_filetime134369086378832275，端口17654/data-root不变。27份权威文件哈希未变，未commit/push。完整报告及前后反例见[整链审查](reviews/giant-recovery-chain-20261020.md)。

- 当前同一100目标累计基数18：原1E600A3F/run1–5共5，上一段1F4FD481/run1–13共13；已逐份核磁盘Completed/三单元/静止回执，并在run13完整结束后维护停止。当前giant-82-recovery-chain-20261020-87d54609b64c433eae648668f2a7dbe9续82轮，新run1已保存Completed，三个Session均Completed、归还1次、金币住宿提交1次、observation.recovery事件0；累计19/100，run2已实际Running并推进跳轮。计算基数仍18，不能把本段run1再加进基数。旧失败/半轮和其它历史100轮排除，审计segments不重复累加。账目/现场在next/.local/chain-261020/resume-ledger.json、completed-audit.json、first-completed-proof.json、resume-confirmation.json；wvd每10分钟监看已更新指针，正常安静，人工及保护停止优先。MuMu渲染器内部根因、旧无HUD哈肯停滞及RESOURCE_UNRESOLVED仍未解决，不把恢复成功冒称底层归因。

### 前次记录

下方保留历史部署和验收索引，不覆盖上方当前实例、账目和授权。

- 2026-10-20重启排查的两处脚本修复已部署并接回同一100目标剩余95轮。原giant-100-input-focus-20261019-d4b588c4a57843c08c8eada4174e11c7在旧1E600A3F实例run1–5逐份Completed共5成功，run6代理维护停止/仅1单元排除。00:23:46 Windows事件1000及64.6MB原转储确认MuMu在libRenderer.dll+0x929ec9以FAST_FAIL_FATAL_APP_EXIT终止，私有渲染器内因仍未解；恢复后Route0_Done迟到无路线结果未交回父调用，60,349ms后独立误重启。现同一auto_route/Call/Handoff显式传递destination_finished，重启入口先走已有ResumeTarget/TargetReacquirePrepared；保留未结算目标及输入保护，不改ROI/阈值/期限。app15入口、完整工厂15正例/2保护反例、目标结算及3个执行器入口通过，失败历史保留，不当作游戏轮数。原构建/打包/manager部署rd-candidate-261020，EXE0ae50bf41dc386abae35a99dccc46519726b49c97a7879ca8afa62112e5bdc22，输入710699e14993dbecfcc4becbb950427f951cae142da8a9d570e7a9b702065d2a/1317。正式17654/data-root不变，新实例1F4FD481-273A-4C2B-B7A2-27085B1EEAA7/PID50816/start_filetime134369033332684226；当前giant-95-destination-handoff-20261020-314252b61d2d42e1b58680689ad8f2de已实际Running/run1，从公会悬赏推进，有新帧和输入，启动时累计仅5/100。只计5+本段实际Completed，旧历史轮数排除；账目及现场next/.local/restart-261020/resume-ledger.json、resume-confirmation.json，[报告](reviews/last-two-recoveries-20261019.md)。27份权威配置/流程哈希未变，未改模拟器/驱动/渲染/VPN，未commit/push；RESOURCE_UNRESOLVED保留，续跑不代表渲染器根因或100轮已验收。

- MuMu专项只读复核：10月19日04:34、08:23、17:33三次system_server崩溃均为TaskUtils.shouldIgnoreDesktopHomeOcclusion+91，经WindowManagerService.doDump。当前安装MuMu6.8.7.0/Android15、实例2为4核6GB/Vulkan30帧；没有CPU/内存不足导致本次崩溃的证据，不能套用历史显卡崩溃调参结论。WVD已退出自己的完整窗口导出调用，不代表MuMu内部缺陷已修。版本及配置来源、官方诊断入口补入[现有报告](reviews/last-two-recoveries-20261019.md)；本次未改参数、未导出上传官方诊断包、未开启循环。

- 最新按用户要求继续查最后run12/run18，不开新循环。Android crash确认run18的system_server在WindowManagerService完整窗口导出路径SIGSEGV，先于元数据失效/ADB closed；原WVD前台和点击门禁调用此路径。现已改为InputDispatcher焦点窗口，保留输入身份/前后夹持及Unknown保护，devices五入口及四组生产执行器定向验证通过，经原构建/打包/管理器部署空闲rf候选：EXE7745626497e1c7ee7701c2a72688d9a5551e9f3f53d6a7653de91aee2f4fb3f3、输入5d84ab235238567c3931279b5e7dde0f525340708d09583442ede2e43a3be5ce/1317。当前1E600A3F-026B-4A10-83F9-221FDE38B175/PID42864/start_filetime134368803533927645，17654/原data-root，run0/Idle/busy=false/无活动repeat；27份权威文件及游戏/系统服务pidof集合未变，没有启动游戏或循环、未commit/push。run12归还坐标/一次输入/菜单消失与16:53:59主动force-stop已核对；隔离真实Application/Service两张原图20项回放确认按钮中心(453,1051)，停滞图的原后置和已知分支均NoHit/无Error，未发现这些页漏接，隔离服务已关闭。但游戏内部停滞原因未确定，不能套用run18结论或宣称全部闭环。证据next/.local/rf-261019/及[报告](reviews/last-two-recoveries-20261019.md)。

- 2026-10-19哈肯路线完成后仍等旧菜单的误停已修复部署。旧A93DE32D/run1–10成功，加此前66共76/100；run11在第二点确认后经恢复退出回HUD，父FirstPoints后继仍要求地图/哈肯菜单，10次主动重启后FLOW_TOTAL_DEADLINE、不计成功。同目标81份终态核得76成功、5次中断（3维护主动停、2自动失败）、14次主动重启请求（本次10），另16应用恢复窗口含前述请求不相加。现按同轮phase/unit_matches/全部点数登记路线完成，原返城独立认新页面/遭遇；Jier/普通地图、输入保护及识别预算不改。C2完整工厂12正例/2反例及recognition19入口通过；初次遇敌夹具的非实际战后菜单顺序失败及未发布C1保留，见[报告](reviews/route-receipt-settlement-20261019.md)。
- 此前正式pt2候选EXE3bfc3b14c7bdda20a3ab1e48bbb7e65c36c0fb3149c416b7f4ddf07361493d95、输入fb13ea5994b3dd43b50d5ca128295cc4f28daf972b2564d3ca7878d5e7731688/1315；历史实例F18FB48A-921E-421E-BF6C-EE4A59572E85/PID32716/start_filetime134368691854080210已正常退出，当前部署以上条rf为准。27份权威文件哈希不变，100轮统计/原帧/源码前态/验证/部署/续跑账在next/.local/pt-261019/；此前跳轮/公会/哈肯交接/LV3保持，run12无HUD及RESOURCE_UNRESOLVED仍不冒称归因。未commit/push或恢复定时检查。
- 当前同一100轮目标已完成：2026-10-19 18:14实核原76份及giant-24-route-receipt-20261019-44522571959e4c849fdb083ccded2dc0的run1–24保存Completed，累计100/100；本段每轮三业务单元、静止、无未决，最终run24 Completed/busy=false/repeat inactive/state=completed/24。没有追加新批次或启用定时检查，27份权威文件哈希未变。本目标105次尝试共100成功、此前5中断；本段无批次中断，但run12无HUD哈肯归还停滞后主动恢复一次，run18旅店入口ADB断连恢复约21秒并报告应用重启。累计15次主动重启请求、18个包含主动请求的应用重启恢复窗口，不相加、不声称连续无故障100。两处底层原因未归因，原帧/日志在next/.local/pt-261019/return-run12/、transport-run18/，最终证据completed-batch-audit.json、completed-run24-result.json、completed-current.json及[报告](reviews/route-receipt-settlement-20261019.md)。

- 本轮原因边界：原公会主菜单guild.menu命中0.994756，但前置Boot和原生resource表漏登记，0输入、空pending下61604ms主动重启；该确定性遗漏已修。独立的旧87BA907F/run2救起后战斗结束，归还输入与成功样本同为(453,1051)，菜单消失后无HUD哈肯展示页约60秒未推进、60892ms触发原有恢复；根因尚未归因，不能称已修或确认官方卡死。该轮按用户要求停止后关闭旧服务，未交账不计成功；死亡/再起及pending原账保留。新轮成功不抹去此独立失败。隔离管理器曾改写生成启动指针的副作用已恢复原17654/data-root并留证，后续不可与正式服务共享候选目录做隔离Start。

- 2026-10-19恢复等待的哈肯交接已修复、构建部署并续跑。复用原harken_floor_menu，在正常面板/迷宫/遭遇不命中后，经已有encounter出口返回原Dispatch；不增加输入、等待或处理器，不伪报治疗/推进任务点，Unknown保护保留。旧原图回放失败，新版三组真实Service/门禁/执行器/巨人导航回放通过：切页1836ms且仅原打开输入，已在菜单0输入，前序目标未完不跳步；recognition18入口complete=true、资源工具6项通过。新独立候选next/.local/hk-candidate-261019，EXE6108abc4ce9dd4abb6698398476bb459d42d7ff02571571ce64c1404b34b4024、输入13792c29ca4649691ed86b7b66364062e9262349849d51524e773880ff5babcb/1311，与回归输入一致。正式实例87BA907F-E0A1-4093-B382-EACDB76F33D2/PID1668/start_filetime134368221936688707，17654/data-root保持，27份权威配置/流程哈希不变。原run1–15保存Completed共15成功，run16按本次用户要求停止不计；新请求giant-85-harken-handoff-20261019-7959df634026457ab4139c4956c95b64续85，累计仅15+新段completed_cycles。已实际Running/run1，从启动推进到跳轮且有新帧与输入确认。启动时公会菜单未接走导致原Boot_Poll一次61604ms主动游戏恢复，独立留证，不声称全程无重启或同条件实机哈肯已复现；旧候选保留可人工回滚，未commit/push、定时检查未改。详见[修复报告](reviews/healing-harken-handoff-20261019.md)及next/.local/hk-261019/。下方未修与初始0轮索引均为此前快照。

- 2026-10-19只读排查当前100轮的run15主动游戏重启：事件3417在恢复面板Open0等待62690ms后触发bound_game.restart，重启前frame381实际为哈肯楼层菜单；frame338/339显示从迷宫接近动画转到哈肯。补给分类/后置没有哈肯交接，上层已有哈肯分支却未取得控制，属于已证实的有效页面漏接，不以超时冒称游戏卡死；未进行交互测试，也不排除同时存在的游戏故障。用户要求不停止循环，本次只保存证据，未改业务源码/配置、构建/部署或commit/push；循环自行完成run15并继续run16，累计15/100。修复尚未实施，证据与调用链见`next/.local/rr-261019/report.md`。下条LV3修复的初始0轮快照仍为历史启动证据。

- 2026-10-19柚奈LV3选级误确认已修复并实机验证。旧原帧水纹遮住3时，全标签将LV5位置(599,1211,60,34)认成LV3，分数0.9012986，随后用LV5的金色边框错误确认；旧独立Service的42项检查有这两项失败。原skill_level现增加同一标签数字核对，保持页面搜索ROI、0.8阈值、点击坐标及既有重试职责；修复后42项均通过，recognition模块c2的17入口complete=true（其中新原帧契约28项），资源工具6项通过。c1测试目录含未登记manifest而失败的证据保留，修复的是独立测试目录装配，没有改弱断言。原build.py辅助函数、package_functional与manager完成最终构建/打包/部署，来源与c2一致：EXE29a531ce7d6271cb5b4879b174d1dccc0378a01047ef496927ee542440c1691f、输入c28ec96c20a410724682ec1d368766f1043715719b56c16dd5aaf116051dbceb/1307。正式17654/data-root保持，实例43E2B469-365A-4D61-8A47-4B3204F4CE0E/PID31632/start_filetime134368150013621320。实机run1/gen2在事件1726按原(379,1228)点击LV3，1731/frame90确认；原帧标题千紫萬紅Lv3、LV3金框及SP233→212/消耗21一致，1758确认释放且策略条目已消费。原100轮请求1352c5ab...在C9F0EB83/run1由代理维护停止，0成功、空未决；当前以giant-remaining-lv3-20261019-a4acdc45cc154b0cb5a994f7132025b6续同一100目标，基数0，启动后已实际Running/run1/0成功，不代表100轮完成。27份profile/保存流程哈希未变，定时检查未改、未commit/push。原帧来源在[测试记录](../next/tests/fixtures/skill-level/cases.json)，回归、失败历史、部署及实机确认见`next/.local/lv3-261019/`的live-confirmation.json与resume-ledger.json；下方部署索引为历史。

- 2026-10-18已按用户指定从实例2当前战斗左上ACTIVE采集“愛莉安諾特”，登记战斗角色`F 爱莉安诺特`。900×1600原图仅裁头像内部78×43像素，避开等级、姓名、相邻头像及高亮框；保持原顶部ROI、完整头像匹配和0.8阈值。素材加入manifest与shared语言分类，角色列表沿Application已有目录生成。原build.py/package_functional/manage_service构建、打包、独立目录真实Service两帧Hit/eligible及目录检查通过；不是实机施放或循环验收。正式17654服务已空闲部署至`next/dist/wvd-next-native`，实例C9F0EB83-7A8C-4C5B-A982-FF9BD16877EA/PID444/start_filetime134368126627957391、EXE28c9511db33571074fa064803bc0ba992bed163334357601e163da8006fde3ff、输入0061a6227dc627a5c2d49dfa0a1fd4ffe415dc061eb9d5786a0f82796e85bdef/1303。核对run0/Idle/busy=false/quiescent=true/repeat=null、输入全0，27份profile/保存流程哈希未变；未启动游戏或循环、未commit/push，定时任务未改。来源见[头像记录](../next/docs/arianoth-portrait-20261018.json)，原帧、独立识别与部署证据在`next/.local/arianoth-261018/`；下列部署身份均为历史索引。

- 2026-10-14导航角色面板交接修复已部署：复用同一recover_in_dungeon及BACK/X，没有治疗需求也关闭已打开的面板；导航页面变化结束旧等待、关闭后重做同一目标，不重复治疗或推进任务点，保留遭遇和Unknown保护。最终hf-c3的app/recognition 31入口complete=true，7个真实Service/门禁/生产图原帧组合通过；其中两张故障图各仅导航及原X两个输入，task_step=0、无新治疗。原正式构建/打包/Validate/Deploy完成，hf-candidate-261014，EXE fdcc6b0ce1edd66c25bc448facfed30b5e2d85321d00d1f155a006dd0b617d76，输入ba011bfacb68f22a483d9ed4ff9fa6dd59fe3ff907079174977185dc1088a1ac/1301；40份用户配置/流程哈希未变，未commit/push。[报告](reviews/navigation-panel-handoff-20261014.md)及next/.local/handoff-261014/保留失败回归与最终证据。

- 2026-10-15按用户要求停止当前实例E9BDAE77-86A3-43D8-BD25-ED08F3B1EE15/PID36068/start_filetime134364625896589622的giant-100-panel-handoff-20261014-16df40b732224180af41c254a89e883c。逐份回执确认17/100，run18为UserStopped/STOP_REQUESTED、busy=false、quiescent=true、repeat inactive、最终pending=[]；不再启动循环，定时检查PAUSED。run1–17无重启事件，run18发生一次模拟器恢复和一次主动游戏重启：Windows事件1000确认00:46:36实例2宿主PID25608在libRenderer.dll崩溃；恢复后免责声明已推进，但加载及标题未离开累计60294ms触发原bound_game.restart，首次标题点击至重启实际25565.7363ms（7次接受），不是角色面板停滞。宿主内部崩溃根因及标题未推进根因仍未归因，不套用旧nvoglv64.dll结论。未改源码/配置、构建、部署或续跑，EXE身份fdcc6b0...仍核对一致；[排查报告](reviews/restart-investigation-20261015.md)与next/.local/rr-261015/保留原帧、系统事件、完整未决账目及停止回执。原启动及首轮5910.88ms证据留在next/.local/giant-100-handoff-261014/；旧批次成功全部排除。下方旧部署/Running仅历史索引。

- 2026-10-13本轮已继续排查并部署恢复途中设备变化修复：MuMu实例2日志三次明确记录Graphics Driver crash / nvoglv64.dll，源码原来在StartApplication途中实例退出后直接把RetryRequired结束为NATIVE_RECOVERY_UNCONFIRMED。现由唯一Coordinator重查原设备并按已证实的退出/断线原因重新选择已有恢复计划，保留原回执、输入保护、取消和不刷新的恢复期限；无状态变化不反复恢复。devices/runtime/app 38入口通过。显卡崩溃的底层原因、哈肯快捷键未开菜单的原因仍未全部归因，不宣称全部闭环。[报告](reviews/lifecycle-replan-20261013.md)及next/.local/lr-261013/保存原宿主日志、连续原帧、验收和部署证据。

- 当前部署5D4EEF14-913D-47E7-816E-E4E7A2F9CAB6/PID47408/start_filetime134363748774208840，候选next/.local/lr-candidate-261013，EXE523ca41423ab06390a4e76a521de30e05dc37b08a376385326330e702421916d，输入3893de6725fa96ffb5b1ca08c80f3018902ae3fc5b40a6f64b1275dc4b4edb20/1301。原build.py/package_functional/manage_service构建、校验、部署完成，端口17654/data-root不变，40份配置/流程哈希未变，未commit/push。当前请求giant-73-lifecycle-replan-20261013-aa12df65f9d6463dba715beeedc378f5已实际Running/run1，越过原哈肯停点、打开楼层菜单、返城及公会继续，已入副本执行新战斗技能；实例2运行，VPN CONNECTED/tun0/VALIDATED。标题连接中曾超过60秒触发一次原有游戏恢复，原事件325/336保留，不声称全程无重启或新重选分支实机故障验证已通过。累计仅基数27+当前repeat.completed_cycles，目标仍100，旧02C237E9/run1–3全为0成功。最新快照next/.local/lr-261013/resume-confirmation.json，launch-ledger及30分钟只读监控指针已更新；不把合同数量当游戏轮数。

- 先前恢复退出修复仍保留：两次BACK后仍在角色页复用已有左下X，自动消失继续、遇敌交回原encounter；X未关闭按原菜单重试，不再五次后直接recover_panel_not_closed停止。31个app/recognition入口及5张真实Service原帧通过；历史实例02C237E9/run3随后因navigation.input_no_progress及恢复途中实例退出失败，原账/原帧见next/.local/wvd-check-20261013-2128/。该实例run1 METADATA_TIMEOUT、run2代理维护停止的Sent原账亦保留，均不计成功。Pause漏识别初判已被反证，未改分支。详见[恢复退出修复](reviews/heal-panel-close-20261013.md)。自然两次BACK出口、独立战斗停滞、红色过渡帧过早party_defeat和RESOURCE_UNRESOLVED不冒称已全部解决；每30分钟仍仅检查留证报告。

以下部署/请求均为历史索引，当前身份、累计和授权以上条及launch-ledger为准。

- 2026-10-13启动页连接修复已部署并续原目标剩余74轮：真实非阻塞连接状态替代ready/socket句柄判断，失效时游戏输入及VPN生命周期均先清理/握手再取图；断线持有触摸不以本机send成功伪报已释放，保留Unknown及发送进度/错误码日志。原5D313D2F/run28失败及完整pending保留；核实例2真实退出后原manage_service正常关闭旧服务，没有任意杀其它进程。最终sc-c3的devices/runtime/app 38入口complete=true、四组真实TCP场景通过；新产品输入9f3b1556f51efc9e8235c8c8621ac1ae124dda0feb6ed29ba98e43f2a34d7768/1301、EXEb7534b59ad356eabd89ac2e4935f8abdf939b0890e9b4b2120a40e9eea2eaa82。新正式实例0DE7D06D-EF0A-407B-8BB6-A2BEF5F5B66E/PID63996/start_filetime134363640750279053、17654/data-root不变，40份profile/workflow哈希未变。请求giant-74-scrcpy-reconnect-20261013-64a3f8786c9c4c5c9f8c161ee9f0489c已实际Running/run1：Boot_Attention事件48接受/54确认耗时3.240秒，后续标题/下载/公告→战斗真实技能→返哈肯导航1635；不是只accepted，也不是100轮已完成。累计仅原26+当前repeat.completed_cycles，旧run19/28失败和39历史排除。每30分钟仍只读检查，不自动commit/push；红色过渡帧过早party_defeat及内存RESOURCE_UNRESOLVED仍独立未修。[报告](reviews/scrcpy-startup-reconnect-20261013.md)、next/.local/sc-261013/。

- 2026-10-13用户纠正停点并明确要求哈肯加护下无损再起：重新取当前帧确认“再起/接受死亡”与右下哈肯标志，沿已有GiantBounty入口的Resurrection_Accept/Continue（run20事件31/42）完成再起，没有点接受死亡、取消或重启。现以giant-82-harken-revival-20261013-2a79fcac35054c5a874a84bf346a3e4db续剩余82次，同一目标仍100，基数18来自原run1–18，原run19失败及旧39均排除；累计18加新repeat.completed_cycles。已实际Running/run20、由旧悬赏整理推进至新战斗（事件2748），用户配置未改，未改源码/部署/commit。原红色连接过渡帧两skull抢先判party_defeat的问题尚未修复，不能声称已彻底修好。证据next/.local/revival-handoff-20261013/current.png、resume-confirmation.json；新100账目和每30分钟监控指针已更新，旧run19人工停点被本次继续授权取代，未来保护停止仍不得擅自抵消。

- 2026-10-13 14:42巡检发现当前新100次批次已保护停止：同实例5D313D2F/run19以party.multiple_deaths_manual_required结束，Interrupted、quiescent=true、repeat inactive；run1–18的保存Completed逐份核对，18/100，run19不计成功。原帧为红色战斗过渡页，下排左右HP=0及死亡图标；繁中至少两个skull的只读识别触发既有多人死亡人工停点，不是低意志力页或自然闪退。本轮无游戏重启日志，未决输入为空，VPN连接正常。证据next/.local/wvd-check-20261013-1442/inspection.json及report.md；新批次launch-ledger已记录停点和报告指纹。仅留证/报告，没有操作游戏、修代码、部署或续跑。等待用户处理现场并明确继续许可；未变停点保持安静。

- 2026-10-13用户随后启用每30分钟异常检查：原wvd聊天定时任务已更新为ACTIVE、30分钟间隔，跟踪下方新100次请求及其独立账目；约30分钟后首次检查，正常无变化保持安静，只报告新异常/100次完成/需要决策。此次检查权限仅只读检查、留证和报告，不授权自动改代码、部署、重启或续跑；人工停止及低意志力/多人死亡/Unknown保护优先。旧每小时任务的批次/身份和修复授权不再作为新监控依据；达到100后暂停监控，不追加批次。下方启动时PAUSED是历史状态，当前以本条及automation.toml为准。

- 2026-10-13用户重新授权一批新的巨人悬赏100次：正式服务原端口已关闭，沿原manage_service.ps1恢复既有低意志力候选，无新构建/部署改动。当前实例5D313D2F-33ED-4BCB-B390-4F0F7EB8BE2F/PID49856/start_filetime134363394955322163，data-root及17654保持，EXE55dadda44d948511fb0b08ca7129ee2733f01c2a8a95f2131a77c56f9acb62a4、输入8324c1cc25ffe341b26e037be983803da602a6d0cf7c0b3080beeaf0a9c4782a。新请求giant-100-20261013-a8c87f231af44fbb8fac3adca6d8dedb/GiantBounty/zh-Hant/repeat_count100，已实际Running/run1/0完成，完成跳轮、入第十区、标记导航并推进到战斗新输入/帧（核对事件1644）。启动账目next/.local/giant-100-20261013-a8c87f23/launch-ledger.json，旧39次成功仅为历史，不加进本新100目标。40份配置/保存流程JSON未变，实例2/VPN CONNECTED/tun0/VALIDATED；定时检查继续PAUSED，未新增采集、外部守护或自动commit/push。低意志力/多人死亡/Unknown输入保护保留，未来保护停止不自动抵消。

- 2026-10-12低意志力停止规则已修复并空闲部署（历史部署索引，当前批次以上方2026-10-13启动为准）：繁中正文两段同时命中后，入副本及启动/公共覆盖页检查沿同一现有ExternalBlocked出口结束批次、保存原帧及未决输入，不点击、不等60秒、不重启或续跑；不是多人死亡页。最终app/recognition 31入口及8个真实原帧/语言/转场场景通过，不是31轮游戏或新实机循环。输入8324c1cc25ffe341b26e037be983803da602a6d0cf7c0b3080beeaf0a9c4782a/1300，EXE55dadda44d948511fb0b08ca7129ee2733f01c2a8a95f2131a77c56f9acb62a4。当时实例666C44B5-49A6-4845-B380-167421649100/PID69140/start_filetime134362146320266936，端口17654/data-root不变，运行EXE在next/.local/lw-candidate-261012；当时已核Idle/run0、busy=false、quiescent=true、无活跃循环，40份profile/workflow JSON逐文件未变，未自动commit/push。[报告](reviews/low-willpower-stop-20261012.md)、next/.local/lw-build-261012/deployment.json保留原验收事实。
- 原563E32FD-B980-4FDC-A473-48244D2F0761/run11人工停止回执仍保存，累计39/100（原29+该请求run1–10保存Completed），run11不计成功。其入副本等待约60秒后的事件565是主动bound_game.restart，事件566原图也是低意志力警告；后续设备恢复不证明模拟器退出根因。原图/回执独立保存在next/.local/low-willpower-stop-20261012/，launch-ledger保留旧请求账目及新空闲服务身份，用户停止优先，不自动续跑。

- 整链整改前次启动记录：GiantBounty/zh-Hant请求giant-remaining-action-first-20261011-e75b589e8353449e94fabf99d94ef20b启动时续71轮，实际Running/run1/0新成功；现已停止，不能当作当前运行状态。启动基数29=原9+前批19+独立实机1，后续已保存10次成功；run20修复暂停与旧22轮等排除。唯一账目为next/.local/giant-100-phone-reset-20261011/launch-ledger.json，不能再次累加completed_segments。
- 前次部署实例563E32FD-B980-4FDC-A473-48244D2F0761/PID62136/start_filetime134362070461905839；EXE7438bc031679cb0e354aa59780c8a88e1122e9a552295c591d8caa80713f59a6，输入386a8776a700d3c7a591f158fc3030dc4aee4de6ebe83d033de273955495f12d/1299，仅为历史索引。100ms强制重做已撤除，同帧结果复用、迷宫/恢复/返城/旅店当前页面优先仍保留。af8四模块52入口与真实截图/保存副本组合通过；不是游戏52轮。af7栈故障定位到测试入口大局部栈并拆分，未扩大栈或冒充历史内存归因。
- 独立实机CD353AB4-5595-44DD-8858-27207BD5FEE2/run1已Completed并保存完整回执，773.370秒、无重启，悬赏及住宿完成。进本步骤至标记接受为0.870秒，旧五轮同口径5.818–7.440秒；本轮未出现宝箱，不能宣称宝箱后同条件实机耗时已通过。一次恢复打开等待11.77秒后转入新战斗，仍列性能复核项，不凭耗时推断原帧或ROI问题。详见[整链整改](reviews/action-first-chain-20261011.md)、next/.local/af-live-261011/acceptance.json。内存仍RESOURCE_UNRESOLVED。
- 2026-10-12按用户要求整理提交与推送：范围为V2工作包、后续已验收的流程/识别修复及对应报告；用户独立.vscode配置不纳入。正式产物由提交前候选构建，source_commit仍记录d03a765，完整代码身份由上述product_inputs_sha256绑定；Git提交不自动重建或重启正在运行的服务。提交及远端确认以Git历史为准。

以下是前次普攻部署的历史索引，不作为当前实例/批次指针。

- 最新普攻修复已部署：旧柚奈普攻保底套用了仅详情页成立的support_selection(absent)，真实原帧为unknown/NoHit，61.308秒触发脚本主动重启；不是自然闪退证明。现已撤出独立普攻点位，复用combat-select-target/skill_target，attack_mode区分无详情普攻与正常单体技能；同一原帧选出(369,281)并通过真实门禁送达隔离后端。at3的49个入口及正式profile四种图组合通过，公共选敌保存定义原revision CAS迁移、用户参数/profile保持。详见[普攻选敌闭环](reviews/attack-target-handoff-20261011.md)。
- 当前续跑giant-100-shared-target-20261011-eda98023979441e996642952a3d61fdc/GiantBounty/zh-Hant/repeat_count100已实际Running/run1，0成功，从Boot_Attention推进到Boot_Title并持续取得新帧，VPN CONNECTED。正式实例0364D50B-F3AB-4790-92B3-76ED27162C39/PID12560/start_filetime134361716761599893，EXE2d707c605e68383bdc11f86b38f0fd74648cda095b275fb52e1e2da843428bfc，输入ce428d74bbae1684626965d3be44c8f7db62c85a031190a3af087d69e6d001f8/1299项；原端口/data-root保持。新100目标沿原launch-ledger，所有0成功修复尝试及旧22轮排除；不宣称已完成100轮或实机无SP攻击成功，待自然日志证明，不自动commit/push。

以下阶段修复记录属于前次部署，当前实例/请求以上述最新指针及launch-ledger为准。

- 当前阶段优先与无SP手动攻击保底已正式构建/部署；49个app/recognition/runtime合同入口、两张真实开箱/奖励Service验证及保存库副本八种整链受控场景通过。两份正式宝箱步骤原revision CAS迁移并回读，profile/坐标/延时/布局及其它参数不变。救人、多人死亡人工停止、Unknown输入和宝石限制保留；没有持续Auto或新增卡死探测。详情与证据边界见[本轮报告](reviews/current-phase-priority-20261011.md)。
- 最新现场：请求`giant-100-phase-priority-20261011-1368d3e0e97c4a858350ffd3827b1b86`/GiantBounty/zh-Hant/repeat_count100已实际Running/run1，成功0；新帧已从奖励继续、返城、公会、跳轮推进到迷宫标记导航。正式实例E93F3276-872F-446C-8774-2D5DB7CDD157/PID16764/start_filetime134361673322405857，EXE080d3d6b5917276d71ef1ece75772813799256c1b4422e1fcb15e6d0414e8abe，输入1a5304e017c3594daa391b3eb45eb4fa79b3a3c85f00d336e3116b9f1dabd010/1299项。端口17654、data-root保持，VPN CONNECTED。账目`next/.local/giant-100-phone-reset-20261011/launch-ledger.json`与小时巡检指针已更新；旧22轮和修复前run3的0成功尝试排除。不宣称100轮、自然无SP普攻或宝箱后同条件恢复延迟已通过；内存仍RESOURCE_UNRESOLVED，不自动commit/push。

## 较早承接记录

下方是历史现场与验收索引，不覆盖上方当前实例、批次或授权。游戏彻底无交互官方bug仍由用户处理，保留现场、不反复重启；其它恢复职责保留。
- 2026-10-11现场继续：用户说明无交互应按卡死处理，并要求先推进看现场。哈肯/宝箱手动探测无反馈，关闭游戏标签后仍相同；真实force-stop已验证游戏PID退出，随后MuMu实例2宿主退出（原因未归因），经原设备连接入口恢复可见实例2。当前正式实例1469931A/PID34172未换版本，单轮请求`giant-recovery-live-20261011-d9f481a67bd74168931a5d0878a86bc7`/run2已实际Running，从黑屏启动返回迷宫并执行返哈肯；Clash Meta VPN CONNECTED/tun0/VALIDATED。仅单轮现场反馈，不重复启动连续批次；累计仍22，完成回执才能增加。新的无响应宝箱探测源码尚未部署，不能当作已生效。此前“等待确认哈肯交互方式”的阻断已由用户现场说明取代，旧停止回执保留。
- 2026-10-11 Windows承接：ZIP SHA256 `6c4d3d97b1863864952a0c4c41aca231137910e2a70d3586bc965f9682aaf88c`、2204份索引文件及64个基线Git blob已核对；累计补丁已应用，`.vscode/`保留。另补7000G顶层拒绝下载范围、独立救人职责声明与真实Call/append owner校验，公共四流程声明同步。六模块contracts-final-7及仅测试夹具变更后的app/recognition contracts-final-8均complete=true；未变模块源/二进制复用证明及合并回执在next/.local/followup-v2-20261011/acceptance.json，当前输入`134747d460b9bc61c68a19422b4f6472acf92cdab3a4ac5e25ed7725d388487e`/1299项。它不是游戏通过或所有RA验收关闭。
- 591项资源载荷已全量核对；五份正式保存流程经原revision CAS精确迁移并回读，用户参数/布局及profile哈希未变。实际保存库20份公共定义接入受控整链，八场景Completed/pending=0；其余五份用户流程未宣称完整执行。正式构建、冻结包、空闲部署及EXE/PDB只读Preflight已完成；没有启动模拟器、游戏、循环或新采集。
- 新100轮累计成功22轮（原4+首次有效续跑18），失败run5/run19均不计数。正常结算后转宝箱、返城Entry遭遇交接修复保留；最新仅修返哈肯静止交接反复重进导航，保留原60秒无进展等待及已有恢复出口。最终47个app/recognition/runtime入口及补充harken-no-route通过，完整构建/冻结包输入d854762d0e61347432f3da388005a177b1ce98273f3b31dcda93cdba4234f557/1299项。正式实例1469931A-0028-4A3D-816E-7134FDDE2AFE/PID34172，EXE602d10c33b82c58038be492beb8e5b90527f25c592399b5971d4a2f1a239c66d；78轮请求giant-78-harken-stall-20261011-1a079d2535ba468db18d878fac291910曾实际Running，但两次恢复仍停哈肯实体前，代理已停止该0成功尝试，服务空闲、游戏/模拟器保留。原Sent标题输入账目保存在session，不伪确认；当前hold_reason为哈肯交互入口未闭合，待合法动作或新现场证据确认后才续剩余78。中间0成功尝试与旧人工停止的2轮排除。[最新现场与修复](reviews/harken-stationary-loop-20261011.md)、[此前转场修复](reviews/target-chest-settlement-20261011.md)及resume-ledger保留原回执与261字符存储路径独立缺口。源码仍未提交，不宣称100轮、宝箱同口径延迟或内存归因通过。
- 用户暂停原批次后授权修复宝箱→恢复延迟、部署并重新开启100轮。正常迷宫/角色/恢复页面排除“再起”OCR，死亡分支先保护；未知页面仍走原OCR。Requested观察并入Begin确认，保留视觉准入和healing_required业务条件，不改ROI、阈值、等待或重试上限。三个模块46个合同入口、两张真实恢复原帧及两张救人/技能详情原帧组合通过；不是游戏复活成功或长期稳定通过。
- 原新100轮批次`giant-100-heal-fix-20261011-0700c9f9f1944e478320b65f7dbdf0d0`及当前剩余78轮均为GiantBounty/zh-Hant。profile revision`d26988cd440cd5f55fbf2ca34e0e4b7b1aaf54dc16d1717d154e0a065b865cab`及SHA`3e873d08fb9135f558c3e3348b3bfffff3b73555c63a9176cc75cff274ccb87c`未改，保存流程未改。沿用原日志/最近240帧策略，不新增WPR/ETL。小时巡检当前累计基数22与新78轮以resume-ledger为准；游戏/救人/Unknown/宝石限制沿用原合同。启动记录`next/.local/chest-heal-latency-20261011/batch-100-launch.md`。
- 新候选实机run1的返城途中战斗结束→打开恢复角色面板3.388秒（事件2771→2793），入口再起OCR调用0，恢复尝试后已继续返城。本次没有掉宝箱，不能冒充旧宝箱后25.1秒的同口径对照；后续自然循环继续采样，100轮尚未完成。
- 补给按[核心合同](../next/docs/core-contracts.md)的尽力尝试语义处理；完整scene成立与独立target可输入分别判断，不能恢复旧的双eligible要求。内存仍为`RESOURCE_UNRESOLVED`，定向分配减少和对象释放证明不等于历史45MiB已归因。

## 先前整改记录

- 用户指定的 `wvd-full-architecture-audit-4c466e1.zip`源码整改/限定离线交付已整理：基线4c466e14fa514fa177380e902afe373c8f20c3b2，54份文件哈希/大小一致。16包不是全部验收关闭，43项未批量closed。
- 架构审计轮只做源码、独立离线/Windows定向验证及文档。随后用户另行授权部署及巨人100轮：已部署并启动正式批次，正式profile/用户流程与`.vscode/`未改；没有新增系统级内存跟踪采集。
- [逐包整改报告](reviews/architecture-audit-remediation-20261010.md)、[43项索引](reviews/architecture-audit-20261010/ISSUE_INDEX.md)及16份JSON回执保留历史验收维度。上一轮整改已提交/推送ee024eb；原回执null是当时状态，不重写历史。
- [后续修复](reviews/architecture-audit-followup-20261010.md)：流程读缓冲OOM句柄泄漏已旧失败/新通过，未跟踪源码漏验与依赖模块映射已修复，跨实例同编号PNG/延迟旧读在实际RunStore通过。六模块后续回执complete=true，产品输入hash60b68ff66c1b5f7733280e6f81cefec56c8f6b9d59dbd148b03906fc64c047e7，随后续源码一并提交；未部署或启动游戏。
- 最终final-contracts-14六模块入口complete=true：39个原生命令、11项工具/构建，另HEAD检查通过；源/10个EXE/4个分析器产物身份复核，产品输入hash为56a9ac97f30875dfd18931f573d2fe57d0616f60f29924111796a1e3a36d242e。这里的50项不是游戏轮数，也不覆盖全部工作包验收断言。
- WP03/07/08/09/11/13/14/15有逐SDK故障、真实ETL/复杂图成本、并发图片端到端、批次边界、全部最小builtin/旧反例等必需覆盖缺口，具体由原回执限定。随后用户纠正补给验收：回复是尽力尝试，不因疗效未确认停止行进；原NoOp断言已改为继续且不虚报疗效。自然崩溃计划仍UNARMED。
- 当前测试入口是 `next/tools/run_contracts.py`，必须指定模块/改动基线和全新隔离输出。个人fork的原生PR工作流已添加，远端CI尚未执行。

## 正式现场基线

- 最近一次部署现场为T0撤回：fedfbf5新增hp_overlay/CloseHpOverlay误把救人页关闭，批次2fa96d84-7998-4c3b-8e7e-6bc234fd00ed/run1已人工停止，0/93。源码完整撤除该分支，原party_death救人代码保持；正式入口当时回滚到该错误分支加入前的完整候选，实例23FC3DF9-FE96-460E-8A5D-049909751C81/pid58992、Idle。2026-10-11承接时17654无服务响应，历史实例不作为当前运行状态。不得自动恢复循环；旧7轮及部分失败轮不计新版完成数，不宣称长期稳定。

- 本批性能排查：前六轮耗时691.060/613.737/1142.091/598.061/509.402/693.269秒，实际模板比对27508–65594次/轮；取图转换合计13.357–28.533秒/轮。战斗Entry平均475–500ms、打开详情等待均值1006–1165ms，尚无持续递增证据。修复采用combat_phase，先当前战斗/详情、再实际转场出口；正式公共打开/结果节点也切换，通用all/any、输入回执及跨帧边界不变。真实菜单/详情/奖励/黑屏的新旧结果一致，菜单ended比对7→1、详情finished比对14→2；缺隐式OCR模型发布拒绝、无效阶段/通用all叶子错误仍Error。证据在next/.local/architecture-audit-20261010/phase-current-battle-frames.log，交接/open/reentry定向验证通过；尚未将这几张图的成本下降外推成实机整轮提速。

- 错误产物fedfbf5/e5094bc8/d2ccfb74已移出正式入口，保留在dist/wvd-next-native.t0-withdrawn-fedfbf5，禁止使用。当前人工回滚候选source_commit=8d3547c（含当时阶段性能修复），输入81fd113333cd13ad02c62e664fec527d4d2733d3682937eaaa3829cb76521bd3，EXE1b58ef43f65b851ed0e8365cdcf070566d665466f7c5e38219af77d39d45e822；不是以当前撤回提交重写旧构建身份。三个正式公共战斗步骤的阶段识别更新保留，profile哈希3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C未变，战斗配置/VPN/日志保持。
- [场景交接修复](reviews/scene-handoff-20261010.md)：上一批2/98后run3目标战斗切到宝箱但外层只接受迷宫，下一战斗使已结束目标失去身份，导致25次重导航并超时。已修目标宝箱终态、统一宝箱阶段条件、宝箱入口转战斗与公共步骤入口交接、战斗/宝箱计时和身份结清；五份保存流程已备份/CAS同步。随后96轮run1已实际确认target_battle_ended_at_chest、task_step1，却在开箱选人后以WVD_CONDITION_DEPTH停止，0/96。已分离条件结构/内部递归、拆出小型布尔求值函数、合并编译时重复包装；真实失败图的完整后置/重试请求、边界/缺图与相关回归通过。新发布4038份请求静态核对最深八层、超限零；这不是4038个测试或游戏轮数。已部署续跑，完成第一轮并自动接续run2；profile/保存流程本轮未改。新一轮没有再次自然进入开箱选人，不能把离线复现冒充该页实机通过；长期运行仍待观察。
- 旧巨人100轮批次ae558656-d04f-4545-a6be-46fa3e4a5f0d在第1轮回复段以supply.healing_outcome_unconfirmed停止，0/100完成。修复后批次96d9fb78-f81c-4ded-ac6a-58034042c72a完成1/100，run2因排查灰色技能重复点击已人工停止、quiescent=true；run1实际完成断点收尾、新跳轮/刷新、巨人战斗、回复、返城提交和金币住宿。不是连续100轮通过，最新以API/日志为准。
- [技能不可用修复](reviews/skill-unavailable-20261010.md)：全技能灰掉被bright_peer条件漏判，公共入口11次点击/约21秒后才防御。新识别改指令栏亮文字参照；同页打不开详情最多补点3次并观察5秒，仍转手动防御、不消费未施放技能。正式保存定义已CAS同步并部署；已实机出现面具灰色技能，两帧识别后不点技能入口直接防御、推进下一角色，检查时11个输入中Auto点击0次。
- [重入与页面交接](reviews/runtime-reentry-20261010.md)：遗留技能详情改自动取消重看菜单；拆锁入口增加已到奖励/战斗/迷宫的只读出口；指令排队战斗补既有逃跑菜单HUD佐证并同步资源闭包。真实失败图、生产图/执行器定向验证通过，保存流程CAS同步并部署。新版已实际进入战斗技能执行；尚未宣称新版整轮、自然奖励交接、遗留详情实机自动关闭或长期稳定性通过。
- 部署修复了候选身份字段陈旧、冻结目录用斜杠拼接导致语义/公共流程/OCR目录漏读、扩展路径祖先误判及根路径表示不一致；没有降低资源哈希/链接/租约安全检查。冻结目录加载和发布复制针对性验证已通过。首次准备失败及首个失败run日志均保留，详见[部署记录](reviews/deployment-100-20261010.md)。
- 最后批次8/41完成、第9轮停止；跨版本/跨恢复累计67/100，不是连续100轮通过。停止前Heal段46.352秒、1199次实际模板比对，取图转换0.334秒、识别调度34.906秒。
- 实机已验证的历史蝎女/巨人流程仍有证据；59项公开目录没有被禁用。Fordraig/Cave工厂是未公开休眠实现，不算新增可运行任务。
- 历史100轮批次的操作许可为部署并启动巨人，实例2、繁中、已有VPN配置，金币可按任务使用；禁止绿色/紫色宝石购买、抽卡、卖装备。当时保持战后/开箱恢复配置。该批次后已发生T0并停止，不能沿用这条历史授权恢复当前循环；疗效未确认不单独阻断的现行语义见核心合同。

## 开放边界

- T0后原只读审查确认救人职责被补给过滤、重连没有本地出口；这些漏接已修且进入当前空闲部署，根冷启动与重连职责仍分开。六个工厂/四份公共流程的独立职责、真实调用及五份保存版本迁移已核对，不推导全部业务调用边及现场接受已完成。重复处理的后续复审基线为至少下载5、Pause2、普通剧情6、旅店退出2、补给提示2、技能取消3处；共享配方、各调用者终点与实际消费情况以继续修复回执为准，不能把重复定义数当成已发生的事故数。原始事故事实保留在[同页多处理器审查](reviews/restart-loop-20261010.md)。

- `RESOURCE_UNRESOLVED`：新聚合器/端点/屏障的离线验证不等于真实负载分配栈归因。后续短采集须单独授权，不再靠增加轮数或PrivateUsage标量关闭问题。
- libRenderer.dll/0xc0000409自然崩溃缺dump/故障栈；历史nvoglv64.dll签名、程序主动重启分别保留，不能混为原因。取证计划保持未启用，厂商SDK契约未证实前不改尺寸查询/缓冲复用。
- 用户已明确回复是尽力补给，沿旧代码点回复、关闭面板、继续；不以法力/道具充足或全员满HP为推进前提。当前部署已移除回复疗效未确认的必停出口；只有实际提交过操作才能消费尝试，送达未知及疗效未验证单独保存，不虚报治疗成功。没有新增HP读取或回复提示OCR。定向验证通过；实机run1事件3969记录回复输入，事件4000记录healing_attempt_finished/effect_status=not_verified，随后完成报告、住宿并续run2。
- 历史面具技能面板图缺失，头像命中不能倒推出技能可点；多人死亡页按用户要求人工停止；部分普通迷宫随机事件繁中素材仍待自然采集，不放行实际依赖缺失的流程。

## 历史索引

- [整改前完整状态归档](reviews/project-status-before-architecture-audit-20261010.md)
- [暂停问题交接](reviews/runtime-issues-handoff-20261009.md)
- [MuMu自然崩溃核查](reviews/mumu-renderer-crash-20261009.md)
- [架构审计本轮交付](reviews/architecture-audit-remediation-20261010.md)
- [架构审计后续修复](reviews/architecture-audit-followup-20261010.md)
