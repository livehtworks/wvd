# 最后两次恢复排查

## 范围与现场

用户要求继续查清100轮最后两次恢复、能修的修好，不开启新循环。本轮开始时正式服务F18FB48A-921E-421E-BF6C-EE4A59572E85/run24为Completed、busy=false、repeat inactive；离线验证后只更新空闲工具服务，新实例1E600A3F-026B-4A10-83F9-221FDE38B175/run0为Idle。设备侧只读取证，不点击游戏、不重启游戏或模拟器、不改profile/workflows、不commit/push。

完整现场及结构化索引位于next/.local/rf-261019/；run12、18的13字段未决输入和恢复事件分别在run12-recovery.json、run18-recovery.json。此前pt-261019/的原帧及账目仍保留。旧100轮累计结果不改写。

## 第12轮：归还后无操作栏

- 输入源Task_ReturnGuild_HarkenExit_Floor，frame348、epoch95，位置(453,1051)。事件3744/3746为一次尝试/接受，不是两次点击。当前菜单的“归还”位于该位置；事件后菜单退出，诊断原帧仅有哈肯场景，没有可识别的菜单、郊外或城市。
- 原未决为Sent、delivery_unknown=false、attempts=1，不是输入连接发送失败。约60秒无后置结果后，事件3809请求bound_game.restart；3816以interrupted_by_application_restart结束原菜单输入，没有伪报归还成功。恢复后4112/4114在新帧独立点击相同真实菜单，4129确认结果，最终完整完成该轮。
- Android退出记录明确列出2026-10-19 16:53:59.986、游戏PID9993、USER REQUESTED/FORCE STOP；与WVD主动停止时刻吻合。系统日志此前仍有该进程的前台关联记录；crash缓冲没有此时的游戏或system_server崩溃，/data/anr现存文件均为10月5日。没有证据把这次归还停滞归因到第二次的系统服务崩溃、内存耗尽、错坐标或已证明官方卡死。
- 当前能确定的是游戏归还转场没有产生已知后续页面，脚本因等待结果而主动停止；游戏内部究竟在等网络、线程还是其它状态尚不能确定。main日志环已没有PID9993当时的游戏日志；不能从事后进程退出记录还原游戏内部栈。保留此缺口，不盲点哈肯、不假定返城成功、不通过延长超时或放宽ROI掩盖。
- 补做同一新候选Application真实Service的两张原图、20项定向回放：点击前floor_scene为Hit，return_target为Hit且center=[453,1051]；停滞诊断图的楼层菜单/归还按钮、原后置（郊外或城市或剧情）、单独郊外/城市/剧情、战斗、迷宫、角色面板、繁中再起全部NoHit，没有Error。前帧另外八项反例也全部NoHit。输入前后像素未编辑，原ROI/0.8等阈值不改，再起实际证据语言为zh-Hant；没有混扫英文。证明未发现这些已知可处理页漏接，不证明游戏内部线程健康或已找到网络原因。

## 第18轮：Android系统服务崩溃

- 原输入是Task_Inn_Open，frame102、epoch8，Sent、delivery_unknown=false；并非付款或多人死亡停点。2996首先记录capture.metadata/CAPTURE_METADATA_NOT_READY，随后2998是pidof游戏的ADB_TRANSPORT_FAILED，设备诊断stderr为error: closed。约20829ms后3006报告application_restarted_menu_reclassified，input_replayed=false、没有实例退出/重启证明，后续重新取页并完整完成该轮。
- 新取得的Android crash原始缓冲在2026-10-19 17:33:20.803记录system_server PID44110的SIGSEGV、空指针地址0x6c。栈为TaskUtils.shouldIgnoreDesktopHomeOcclusion → TaskFragment.getVisibility → Task.toFullString → TaskDisplayArea.dump → DisplayContent.dump → RootWindowContainer.dumpDisplayContents → WindowManagerService.doDump。随后才出现元数据不可用和ADB closed。手机/宿主当前时间已核对同为+0800，不混用UTC。
- 这是Android系统服务在窗口信息导出路径内实际崩溃的证据，不是单帧静止猜测、WVD EXE崩溃或MuMu宿主显卡模块崩溃。17:33:34另有uiautomator进程Shutdown thread崩溃，时间更晚，不能倒因为最先的系统服务崩溃。
- WVD原foreground每次调用完整dumpsys window，输入context_probe_command又在viewport前后各调用一次同样命令；本工具确实持续进入栈中窗口树导出路径。crash栈没有Binder调用者身份，不能声称已排除所有其它调用者，也不能把修改客户端查询宣称为修复MuMu内部空指针。

## MuMu版本、设置及重复故障核对

- 同一份android-crash.log另保留2026-10-19 04:34:40.724和08:23:37.526两次system_server崩溃；连同17:33:20.881，三次首帧均为TaskUtils.shouldIgnoreDesktopHomeOcclusion+91，均经过WindowManagerService.doDump+1558。这是同类窗口导出崩溃重复发生的证据，不是三种无关故障；崩溃日志仍未标识具体Binder调用者。
- 本次只读核对安装记录为MuMu 6.8.7.0、Android15；install_config记录主程序1.8.7.5774、player15.8.7.5776、vms152.0.49.003、renderer1.0.99。实例2实际vm.cpu=4、vm.memory=6，shell配置Vulkan/vk、30帧。当前配置不等于此前每次故障时的配置，也没有证据证明本次空指针由CPU/内存分配不足造成。
- 设置来源为安装目录configs/install_config.json及vms/MuMuPlayer-15.0-2/configs中的vm_config.json、shell_config.json、customer_config.json；注册表安装版本与宿主shell日志版本相互核对。配置中设备标识不复制到报告。没有修改设置、切换Android版本或更新驱动。
- MuMu[官方问题诊断说明](https://www.mumuplayer.com/tw/help/win/graphics-card-driver-crash.html)提供右上菜单/帮助中心的问题诊断入口，可将诊断信息和截图交官方支持。其切换渲染/更新驱动建议针对显卡故障，不能直接当作当前窗口管理崩溃的修复证明。当前已保留宿主日志、Android原始栈及WVD时间线，但尚未导出或提交官方诊断包，也未获得官方对具体版本缺陷的确认。函数名中的DesktopHome只作官方排查线索，不据此擅自关闭某个设置。

## 已实施源码候选

同一DeviceSession改读dumpsys input中的InputDispatcher焦点窗口，不再调用完整dumpsys window。android::focus仅接受FocusedDisplayId=0及该显示器FocusedWindows中的真实组件，不拿FocusedApplications、FocusRequests或无窗口当作前台；系统弹窗会返回其自身包或未知，而不是后台游戏。取图元数据从同次输入服务结果读取焦点及viewport；实际输入仍保留焦点前→viewport→焦点后的夹持核对、各段返回码、帧/连接/尺寸/旋转身份和未决输入保护。不增加兼容查询、隐藏fallback、点击或恢复次数限制。

来源：[AOSP WindowManagerService](https://android.googlesource.com/platform/frameworks/base/%2B/refs/tags/android-16.0.0_r3/services/core/java/com/android/server/wm/WindowManagerService.java)说明完整导出会进入dumpDisplayContents；该参考版本并非MuMu的完整源码证明。[AOSP InputDispatcher](https://android.googlesource.com/platform/frameworks/native/%2B/171cac1b603e4bb83412eb596d05a500af5d7a76/services/inputflinger/dispatcher/InputDispatcher.cpp)给出FocusedDisplayId与FocusedWindows的实际输出实现；本机MuMu15原始输入服务输出另外保存在tests/fixtures/android-input/mumu15-input.txt，来源/哈希见同目录source.json。选择的是现有Android字段，不是新造的行业术语。

## 验收状态

- devices模块五个入口全部通过，complete=true；新增入口含真实输入服务原文及15个焦点检查，并验证分段读取和焦点切换。系统弹窗、无窗口、错误显示器、重复/冲突、FocusRequests不能冒充当前窗口、截断边界均有覆盖；物理输入门禁、生命周期期限及真实TCP控制连接测试保持通过。证据next/.local/rf-dev-c1-261019/receipt.json及rf-261019/devices-c1.log。原来的设备/识别旧测试未删改弱化。
- 复用原构建入口正式构建automationd及test_native_flow；恢复后计时、Unknown输入人工停止、实例退出证明因果、结果确认四组生产执行器定向用例通过。使用隔离设备或内存输入端口，不连接正式服务写测试数据；不是四轮游戏或实机崩溃复现。证据candidate-proof.json、build-candidate.log及所指日志。
- 构建冻结/打包与已验证源码一致：产品输入5d84ab235238567c3931279b5e7dde0f525340708d09583442ede2e43a3be5ce/1317，候选next/.local/rf-candidate-261019，EXE7745626497e1c7ee7701c2a72688d9a5551e9f3f53d6a7653de91aee2f4fb3f3。原管理器核对旧实例后正常关闭旧WVD服务，仅更新17654/原data-root的空闲服务。新实例1E600A3F-026B-4A10-83F9-221FDE38B175/PID42864/start_filetime134368803533927645，run0/Idle/busy=false、没有活动repeat；27份profile/workflows哈希不变。
- 部署前后读取游戏pidof集合33298 33377 33378、system_server PID30436，均一致；未执行am start/force-stop、模拟器启动/关闭或游戏输入。本轮只有ADB只读取证，没有启动新循环或故意触发系统服务崩溃。最终快照idle-current.json、idle-service.json、authority-after.json与game/system-pid-before/after.txt。
- 原图回放独立复制候选至rf-probe-candidate-261019，独立可丢弃data-root rf-probe-data-261019、端口18787，未调用设备连接或任务入口；Application上传原图生成的probe-snapshots只写该隔离根。实例E7F008D1-7BED-4444-854F-82AA770E81C2/PID44012核对run0空闲后经原管理器关闭，无测试后台残留。正式rf候选的service-launch.json未与该服务共用；证据return-frame-probes.json、return-frame-probes.log及probe-start/stop.log。这20项不是20轮游戏，也不作为系统服务崩溃复现证明。

## 尚未闭环

### 10月20日新100轮的独立故障

本次新批次giant-100-input-focus-20261019-d4b588c4a57843c08c8eada4174e11c7的run1–5已逐份核对Completed、result_saved=true、三单元结算；run6于用户本轮授权下维护停止，不计成功。新证据目录next/.local/restart-261020/，与此前已完成100轮分开。

1. 00:23:46 Windows事件1000记录实例2宿主MuMuNxDevice.exe/PID47052在libRenderer.dll+0x929ec9崩溃，Report ID为0a57d612-0e0c-44b7-b11a-6dfc024533e5。保存了64,641,402字节原始转储及独立哈希。CDB异常记录为0xc0000409、参数7/FAST_FAIL_FATAL_APP_EXIT，栈经过libRenderer、VaddressDevice及VBox线程；只有导出符号，不能把巨大偏移的最近导出名当成真实内部函数。[微软说明](https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail?view=msvc-170)要求结合第一个异常参数解释，不能只凭通用标题认定栈溢出。私有模块的主动致命退出尚未定位到具体内部检查，不证明显存/内存不足、WVD取图致因或昨天的Android窗口管理崩溃复发。原恢复79,934ms重连实例并回游戏；观察到的实例退出证明、原pending及原始恢复原因保留。
2. 恢复后又发生独立脚本误重启：run6事件2736/2738提交一次标记导航，2742在frame425确认一般迷宫后置；2748的Route0_Done随后才识别无目标提示，2753子流程正常结束。父CallRoute0只接受输入结果帧的navigation_no_route分类或已结算战斗，因此未接住稍后提示，等待60,349ms后2857主动bound_game.restart。维护前截图diagnostics/1.png及两张输入前后原图已另存；这处不归因给宿主崩溃。
3. 快捷目标调用现有auto_route时显式接收destination_finished：已识别的无目标提示/提交后到达通过原Call/Handoff交给父任务，再走原导航结束与任务点结算。未决输入、尚未结算战斗的拒绝、哈肯到达、普通导航/采矿的原语义保留，不改变ROI、阈值或恢复期限。
4. 新增生命周期恢复场景实际暴露第二处缺口：EntryReady直接进入EntryNavigation，绕过已有ResumeTarget/TargetReacquirePrepared，残留旧目标身份使原TARGET_ENCOUNTER_RESULT_UNCONFIRMED保护正确拒绝继续。现先由原目标进度分支承接，再走普通导航；没有撤销上一项修复，也没有放宽保护。失败夹具和日志保存在linkage/及build-candidate.log，修复后的结果在linkage-final/及build-candidate-final.log。

本轮验证分别为app模块15个入口complete=true、完整工厂巨人链15个正例与2个保护反例、target-chest-completion及confirmed-result/child-result-frame/unknown-input-manual-stop三个执行器入口；测试只隔离设备/语义叶子，不冒充实机崩溃复现，也不累计为游戏轮数。首次构建期间源码变化被拒绝、首次夹具节点名称错误及随后恢复入口真实失败均保留，未改弱断言。最终源码输入710699e14993dbecfcc4becbb950427f951cae142da8a9d570e7a9b702065d2a/1317，候选rd-candidate-261020，EXE 0ae50bf41dc386abae35a99dccc46519726b49c97a7879ca8afa62112e5bdc22，原build.py/package_functional.py/manage_service.ps1已构建、打包、部署；未commit/push。

正式服务仍为17654及原data-root，新实例1F4FD481-273A-4C2B-B7A2-27085B1EEAA7/PID50816/start_filetime134369033332684226。按原Application提交giant-95-destination-handoff-20261020-314252b61d2d42e1b58680689ad8f2de，续同一100目标剩余95，基数只计旧实例run1–5共5次；run6维护停止排除。已实际Running/run1，先后有公会Report输入及结果、TimeLeap_SelectTargetEn结果580和LeapEn输入589、新frame285跳轮画面；此时累计仍5/100，不冒称新段已完成一轮。运行事实在next/.local/restart-261020/resume-confirmation.json及保存Completed回执中。27份权威配置/流程在提交和运行后逐份核对未变，未修改模拟器/驱动/渲染/VPN设置。MuMu私有渲染器的致命退出仍未归因，不能以脚本修复或续跑成功宣称解决。

原wvd监控已通过应用工具更新为每10分钟检查/证实故障修复后续剩余轮数，ACTIVE；正常安静，完成100即暂停，不自动commit/push。人工停止及低意志力、多人死亡、Unknown输入等保护停点不自动抵消。计划读取本轮resume-ledger而非10月13日旧批次，不新增外部循环守护或旁路执行器。

第18轮的直接故障已定位为系统服务窗口导出栈崩溃，WVD自己的相应调用已退出生产路径；没有修补MuMu私有系统代码，也不保证其它软件不会再次调用该路径。第12轮的游戏内部转场停滞仍未定位到线程或网络原因，不能宣称一起修好。以后如在用户另行授权的正常任务中自然再次发生，需要在恢复前同时保留独立ADB原图与游戏线程/网络现场，区分游戏停滞和取图停滞；本轮不擅自开循环制造样本，也不改变输入结果或超时条件。RESOURCE_UNRESOLVED保持，游戏退出时2.6GB RSS不证明本次是内存泄漏或OOM。
