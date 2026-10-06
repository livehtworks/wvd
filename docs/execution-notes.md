# 当前执行注意项

- UI设备connected=true仅说明backend对象仍存在，不能证明已关闭模拟器存活。candidate121冷启动复用旧backend时，初始EnsureVpn遇instance_exited/connected=false报PRECONDITION_MISSING，根任务NATIVE_INITIAL_LIFECYCLE_UNCONFIRMED且输入0；该源码缺口尚未修。用户授权拉起时可在quiescent且device lease可释放后，通过正式disconnect完成→connect原实例重新绑定，不关闭其他实例、不并行开启旧脚本；运行中的设备故障仍由原恢复链承接，不能在busy时强行重连。见`reviews/giant121-50-start-20261007.md`。
- 内存分配栈当前入口为`collect_memory_stacks.ps1`/`export_memory_stacks.ps1`：PID Heap Snapshot只覆盖启用后栈。Collect显式提交两个各1轮任务；Monitor只读附着已运行批次，不提交或停止游戏。`MonitorCurrentRound`将首个收尾标为部分采样，下一轮才是完整窗口。独立证据目录、20分钟/512MiB和目标身份约束仍有效；跟踪结束只清理本次PID/会话。
- VirtualAlloc仍是全系统记录后筛PID，不存在已验证的录制进程过滤。空闲probe通过不证明游戏中吞吐可控：首次实跑Monitor的内核文件在96MiB封顶，提前保存仍因rundown写入失败；该回执失败、未获得收尾边界，不能归因。用户另行授权的新窗口明确采用`CaptureKind=HeapSnapshots`，移除VirtualAllocation事件与栈及对应导出表，不是偷偷重启采集或隐藏fallback。仅堆窗口不能覆盖VirtualAlloc、启用前堆块或解释全部进程私有提交增长。旧437MiB/136897丢事件和新96MiB失败均保留。详见`reviews/memory-collection-repair-20261006.md`。
- WPA exporter11.7的`-symbols`属于`-processor 'Event Tracing for Windows'`的输入参数；只设置_NT_SYMBOL_PATH时会导出Symbols disabled。退出0可能仍包含局部表导出错误，必须核对CSV、目标PID、快照数及非空栈/应用符号。源码定位的Heap Snapshot表GUID/列GUID已按本机SDK元数据核对，不能拿Heap Allocations表冒充快照。缺系统DLL/CRT符号单列missing，不隐式批量下载所有进程PDB。
- 实跑Heap-only在20分钟内仅取得一个部分轮join；轮间prepare至少约8分钟，不能据active预报下一轮及时收尾。部分WPA导出去掉地址列后仍膨胀到2.071GiB，不能把视图列当实际按栈聚合。输出保护也要用共享打开文件流的Length而非FileInfo缓存，并在子进程结束后核对；轮询保护不是硬磁盘配额，新版本尚未重跑活动写入验证。保留超限CSV和失败索引，勿把2GiB残缺CSV整份Import-Csv进内存。详见`reviews/memory-monitor-two-20261007.md`。
- Release分配栈候选需真实`/Zi`与链接`/DEBUG`，按DbgHelp核对EXE/PDB GUID及Age；同名重新构建PDB不能解释历史候选。PDB文件匹配不等于WPA实际符号加载。本轮原119符号只读保留，121匹配符号另组，candidate120未部署。
- 英文OCR字典的锁定哈希对应原CRLF字节，Git checkout的LF副本可能不匹配；先比对已验证运行资源，复制同一锁定字节，不修改manifest/依赖锁绕过。ONNX租约只按原HANDLE流式验摘要，真实OCR及`bytes()`路径须分别检查；所有者断言应区分全部哈希字节与常驻非模型字节。
- 原生author恢复夹具须推进新的连接代次并装配原PublicStepScope；输入保护夹具使用当前任务配置和六角色目标，不能因旧`next`值失败而删断言。MSVC JSON与string比较用显式`get<std::string>()`。初次失败日志保留，最终修正不得改变生产输入门禁。
- 本次managed worktree完成checkout但ignored AGENTS.override扫描挂起；只结束本次可验证只读扫描，不杀其他Git/应用。attach归属验证失败时保留已核对的独立checkout，不声称应用已附着；构建/补丁证据按实际路径记录。

- movement_stopped嵌入多个all条件时，外层请求缓存不能保证子采样一致；静止结论按完整帧身份保存，跨新帧续读须复核场景、小地图及输入epoch/设备/连接/视口/导航作用域。初次采样和等待旧帧不证明静止，反证立即失效，3秒周期/ROI/阈值不变。`--movement-frame <900x1600 PNG> <完整冻结包>`须包含实际OCR模型；源码pack子集缺模型时报真实missing_resource，不允许跳过Error。真实auto_route回放记录38次识别约25秒，原15秒测试墙钟不足，独立测试窗口35秒不改变生产预算或TTL。见`reviews/linkage-aa0957a-20261006.md`。
- 目标战的再起/宝箱/阻断必须回原目标处理上下文，不能返回普通Dispatch丢失战后链；原目标身份不明时保留未完成，不把任意新遭遇结算为目标。短暂无路线提示通过当前调用帧的ConfirmedInputResult交接，消费一次；历史结果无点击资格，实际新输入/另一调用清除，祖先pending和身份/epoch检查继续有效。超时known_scene只能复核当时声明的合法候选，不能扫描整个Definition跳阶段。
- 内存仅有工作区估算和释放后OS数字不足以归因。现沿释放边界记录Service/OCR/Session活对象、资源租约ID/共享引用及Held::content原文件缓冲、程序参数容器规模；OCR初始化/销毁配对记录受原memory/debug开关控制，不每帧扫描。相同租约去重，容器容量估算不当作常驻分配；释放后余量仍unknown。`analyze_memory_owners.py`只读输入、输出独立，旧日志缺字段标missing，不补造对象计数。

- 友方六卡的增益图标会将左上外轮廓拉高，不能只从左列起建网格；中/右列也可锚定，缺轮廓格仍要求四条真实边。多个锚点的同一物理网格允许小像素差，真正多布局/卡片缺失仍拒绝点击。一级无等级按钮是正常handoff，父级也须承接角色改变、详情关闭和战斗结束。am start成功不等于前台已到达，须新状态核对并在原期限内补发。
- 应用在Boot内再次重启可能留下旧Title待确认输入及同根恢复事件帧。新Boot结束后只允许同owner/同事件/同处理器/同replan目标的device-restart链退出，旧pending仍逐一核对原后置和身份；不得删除通用跨处理器检查或把boot_ready当旧输入成功。Title需要在原结果期限内按新帧补点，因为logo出现不表示Tap to Start已可交互。第29轮实帧/事件见`reviews/repeated-boot-recovery-20261005.md`。
- 公告标题+关闭可用组合条件确认页面，但any/all不提供定位中心；按钮必须用独立模板定位，并复核正式probe的center，不能只检查Hit。新增`vision::resource`须同时登记`location_probes.hpp`的缓存索引，否则正式任务准备会报invalid map key。候选114公告新增真实柔化字形模板，不使用会命中空白的亮度掩码。`test_native_author --boot-progress`需指定隔离`WVD_CLOSURE_ROOT`。
- 无路线/无宝箱提示不能保留英文引用再依赖繁中语言门禁：门禁会直接NoHit，正式入口须先解析对应语言配方并检查实际依赖。此类短暂提示须在输入后首帧分类并交给上层；普通子调用返回不丢观察周期，事件返回、输入后及身份/TTL失效仍必须重新取图。标记步骤终止推进返程，不代表返哈肯成功。无宝箱未采集，不能拿无路线素材替代。公共流程写入resource_locale后必须refresh_images，否则资源索引会过期。
- JSON单元素条件数组显式用`J::array({condition})`；`J{condition}`可能调用拷贝构造保留对象，后续push_back会异常。PipelineCompiler::finish移动出图，不得对同一compiler调用两次。多人死亡选择按用户要求停止人工处理，不把英文选择提示注册为shared；繁中多骷髅仅作保守停止，不可授权点击或重启。
- 识别缓存须先解析最终语言配方；语言排除和组合识别没有best_score，不能按单模板读取或复用。跨阈值缓存仅收真实score/box测量，直接one()辅助识别也须走同一解析，不能让资源依赖选繁中而运行仍加载旧英文。多人死亡停止事件须同时进入战斗内层checks.inherit及正式保存公共定义，只有根处理器不够；修改保存定义经CAS/备份，不替换用户节点/边。
- 通用叉号只定位关闭控件，不能独立充当普通面板身份。candidate110/run8单人救人叉号被close命中0.845762，pause_negative遂Hit并否决party_death；candidate111仅让close排除Pause，不让其证明普通面板。真实故障原图完整party_death正例及城市/友方详情反例通过，不把单模板Hit当救人流程通过。result.json才是终态，run.json仅定义；crashes业务计数为0也不能代替重启尝试事件。
- 生命周期ADB超时可能已生效，临时传输错误只在原步骤期限内重新观察绑定实例/应用，再决定是否重试，不重置期限、不重放游戏输入；非传输错误仍退出，停止仍优先。内存压力进程榜只在会话首帧/30秒高压力检查及失败收尾采集，沿用memory/info开关；必须记录不可读/截断，不能拿进程私有提交总和当完整系统提交或把历史峰值当当前用量。

- `RiseAgain`观察用途经`party.revival.page/revival_prompt`先排除正常战斗，定位用途仍是`party.revival.action`精确唯一OCR。通用any/all不改为短路吞Error。SP/MP不足OCR必须携带`mode=ocr`；正常菜单/详情及无NEXT的战斗动画先排除，不把攻击动画当错误弹窗。真实弹窗文字和再起正例未确认，不能宣称通过。
- 技能公共定义的未进展失败边必须源码、正式保存流程同时接入，正式写入经revision CAS及备份；UI线的失败语义是`data.kind=failure`，仅改`sourceHandle`不会产生失败边。20秒是本次同页打不开详情的降级窗口，不是整条任务重试次数限制；仅多次已送达、新帧仍命中原角色/原页、无未知送达及副作用时结清为no_progress，改手动防御但不删技能行、不记释放成功。未知页仍交60秒异常恢复。
- 应用重启时挂起的子调用期限不能杀Boot；活动恢复/恢复后重新规划先于悬挂调用期限检查。WVD重启事件明确重规划到根Entry，保留业务账目，由game_restarted确认重置战斗选择，失效菜单子调用退出；已提交受保护技能/未知送达仍不得借此重放。检查`test_native_flow --exception-restart`包含过期子调用，`--critical-recovery`继续保护未确认副作用。

- 救人中心连点的后置使用已有`party_death_post`有序分派，仍是救人页时优先继续同页输入，不能在每次连点前穷举再起OCR/启动场景。每次输入均新帧门控，100ms仅为节流，不承诺端到端每100ms一击；页面变化后禁止延续连点。姓名死亡提示可能被Pause中心文字布局误判，救人处理器离开漩涡后须在原期限内只读重观察，不因一次input_clear否决立即失败，也不在过渡页继续点中心。任务重新启动时的Boot须将再起页交还原业务复活流程，不能只认boot_ready再将正常再起当异常重启。`RiseAgain.png`含英文文字，繁中需经`party.revival.action`语义配方；2026-10-06真实再起页OCR0.999548及实际点击回执已确认，但随后黑屏/重连的旧输入未确认，不能据此声称完整实机再起/续行通过。

- 异常处理器须在整条嵌套调用的checks.inherit中保留，不是只在战斗外层声明。角色、Auto或技能公共子定义过滤`wvd-party-death`时，即使原图party_death=Hit，也不会触发救人；需同步源码和正式保存公共定义，并用实际执行器进入子调用后注入叶子观察验证。正式停止接口是POST `/api/v1/runs/current/stop`或`/api/v1/runs/<id>/stop`；`/api/v1/runs/stop`被当作非法run_id，不是停止成功，必须复核UserStopped/quiescent。

- 目标战斗成功后的延迟内可能立即插入宝箱，不能仅接受普通迷宫后置，再回Dispatch丢失已成功路径。保留战后确认链直到新鲜副本场景确认；未知页只读重观测，事件仍走原处理器。黑色继续按钮需先匹配轮廓，再检查字形亮度；现有留存可用态最高亮度仅165，不能用180误判全部不可用。路线到达只在已发起导航后联合小地图停止确认，初始黑按钮不跳过导航；回哈肯继续使用菜单/退场证据。

- ACTIVE与技能菜单出现不证明当前行动头像已展开稳定。面具frame178为未高亮过渡形态，完整头像0.638，扩大左上ROI仍0.638；稳定帧0.974。prepare没有身份命中必须重新取帧，不能与已认出且无配置动作共走Auto。新增业务摘要字段必须同时接入business_condition白名单，并用正式条件接口复核，不能仅测摘要/图编译；candidate101漏接失败保留，candidate102修正。

- 哈肯楼层菜单随可用楼层数量改变排版。第三章实帧“移動”框为[36,211,76,30]，旧y=230起的ROI裁掉标题，使已到哈肯误留在Moving并触发60秒异常重启。标题范围统一[0,100,250,400]，归还范围[200,400,500,1000]，原生页面/点击与作者语义素材同步；仍要求移动标题和归还同时成立，不用单个通用返回按钮证明场景。
- prepare的has_prepared_skill=false不代表头像未识别。本轮主角0.97351命中但一次动作已消费，须分别记录portrait_recognized、recognized_portrait、best_score和remaining_actor_actions。没有剩余动作、整组用尽、明确全自动、真实低分分别诊断。用户已确认candidate97的白色Auto是人工关闭，程序后续又开启；必须撤回程序补关成功，不把观测状态当作输入因果证明。旧AutoThisChar两次同步ADB input tap之间Sleep(0.5)，之后Sleep(2)；scrcpy同连接快速DOWN/UP双点击仅复制数字不证明行为等价或单角色隔离。

- 单角色Auto与持续Auto必须区分。用户已明确禁止500ms，当前脉冲为100ms；编译器、程序校验、输入门禁和scrcpy编码统一拒绝旧500ms参数。第二次点击送出后无固定等待，立即用新帧检查白色/黄色模板；白色且非黄色才确认关闭，黄色且非白色只补关，歧义不放行。2秒收敛等待仅在确认关闭之后，不推迟首帧颜色检查。停止中断不继续输入，不重放整组脉冲；不把状态由白变黄或人工关闭归因于程序成功。技能选敌失败不得取消并转Auto。

- 战斗打开详情不能把原菜单当后置成功。固定点菜单补试与模板点击复用唯一输入回执，需同角色、原菜单、后置未到及combat输入门禁；动作只送达不等于技能成功。自动保底确认必须保留未施放技能及整组，推进epoch以拒绝重复结算。
- 动作历史图沿RunStore原线程保存：四张待处理+一张在途，关键帧PNG绕过15秒周期采样、同帧去重，240张/128MiB滚动上限不变；消费queued事件时仍须核对实际文件。事件文件长度用Get-Item.Length并解析复核，不能把一次错误读取当成落盘缺失。

- 当前行动头像识别必须使用已裁好的完整角色素材，不能把整图/右半/右上/上半独立取最高分作为输入授权。保留顶部ROI的位置容差，不扩大到排队头像；prepare须记录best_box/search_roi/crop/identity_basis。当前柚奈帧完整匹配0.926、艾妮琪完整0.617而右上裁片0.782；缺失的历史frame183不能靠不同时间原帧冒充复现，需区分已部署防护与未归因的历史瞬间。
- 战斗编译不能展开STRATEGY中全部保存方案；只编译冻结运行可达方案（任务点以及启用的特殊敌人规则也必须覆盖），保持首个同名分组语义。巨人真实配置已暴露4608节点限额误触，不通过提高上限掩盖无关方案展开。要塞启动就绪与退出公会均用通用城市证据，具体到城仍由独有背景证明。
- 白色图标/ACTIVE文字的背景可能造成普通模板漏匹配。遮罩必须使用作者已支持的`mode=bright_mask`，不能把参数随意附加到template绕过作者契约。ACTIVE遮罩会在明亮城市背景上高分，须与独立战斗倍速HUD或技能明细联合确认；技能弹窗会遮暗倍速。不能单独降低阈值，新隐式素材须同时纳入编译资源冻结。
- MuMu管理器is_process_started/is_android_started为true及ADB get-state=device不证明实例有响应。本轮实例2窗口Responding=False且shell echo挂起，正式准备ADB_TIMEOUT；现有退出型重开不承接此种存活挂死。只读诊断命令需有界并收掉本次会话，不能动其他实例或把挂死伪称退出。

- 跳轮完成后的post_delay与输入后置确认期限不是一回事：父级场景已确认则无需另等10秒，不能因此缩短慢网络窗口。定向入口`test_native_author --post-leap-ready`须装配原PublicStepScope并设置独立WVD_CLOSURE_ROOT。任务源`_TIME_LEAP`是显式跳轮步骤，优先于外部默认选项，不写回profile；第三章巨人沿要塞原地续段，不旅行王城。默认非矿石蝎女的ReturnFortress必须绑定`encounter/stopped`并交RecoverReturn，否则正常遭遇会在图编译时暴露未接出口。

- 标记快捷导航的停止/无路线不是击杀证明。巨人目标采用成功Battle回执→可取消3秒等待→新帧确认→快捷返哈肯，不能复用普通mark_auto到达后的AutoMap坐标验证。两种路线在同一traverse_dungeon内按任务点语义分支，保留统一战斗、宝箱、事件和输入所有者；不复制第二套执行器。

- 战斗动作列宽与表头使用同一grid定义，窄屏只允许skill-table内部横向滚动；工作台限宽1140px，普通表单按内容宽度flex换行，路径/列表保留整行。紧凑样式限定workbench-page，不改变流程编辑器。缩放检查注明CSS等效视口，不将其说成已操作原生浏览器缩放。设置Tab不是保存scope：常用参数内含common与combat两个独立草稿，导航脏标记必须覆盖两者，不因合并界面改为整份保存。

- 头像采集从原900×1600帧裁内部像素，名字、等级、相邻人物和当前行动高亮不入模板；不可把采集坐标当运行固定格位。已有蝎女素材不当作丢失重建，像素相同副本只为导入集合整理。身份不明或分数贴近门槛的图仅登记候选，不自动绑定方案；有限跨帧分数不是全游戏准确率。

- 战斗编辑拖动沿SortableJS的独立handle；结束时先恢复DOM，再修改Vue数组，不能同时让DOM和Vue成为顺序权威。序号不写入配置，原数组顺序就是优先级。怪物上传只接受小头像或900×1600原图行动条裁切；PNG压缩体积、头部尺寸和解码尺寸都受限。怪物规则在常用参数保存，方案本体在战斗方案保存，添加时须引用已保存方案。
- 战斗调试只在新鲜帧确认游戏前台且处于战斗后接入原执行器；不能把API请求已接收当作开始成功。正式部署保持Idle、不刷新用户现有草稿页；本轮真实施放由用户点击调试验证，不替用户开启循环。

- 工作台局部保存由 catalog.profile_save_sections 定义字段，scope 为 task/common/combat/advanced；不能从浏览器整份草稿合并其它区。任务目标变化须先保存任务区，服务端按新目标读取有效配置，不复制旧任务覆盖。Playwright 取消 beforeunload 使用页面内 window.location.reload 触发并 dismiss；page.reload 等待取消的导航会超时，不能误报应用丢失草稿。正式网页有草稿时不自动刷新，隔离服务方可写入验证。

- Playwright定向参数使用`node node_modules/@playwright/test/cli.js test ... --grep ...`，或`npm exec -- playwright test ... --grep ...`；不带分隔符的npm exec会吞掉grep并跑整文件。旧closure夹具固定18754且缺少当前部分API，不能用18758或将夹具拒绝请求造成的NETWORK_ERROR视为业务回归；真实profile验收沿独立目录原生服务。

- 技能文字会改变弹窗高度：友方明细可在y=687，巨人战斗三重霞明细框到y=1207。详情、确定、关闭共用全宽下半屏并上扩至600的`[0,600,900,1000]`，不能用固定右侧窄带或底部一行。点击取识别框中心，不取ROI中心；倍速、行动条、NEXT不套用此范围。旧close包含英文Close，友方结构只裁共有叉号`[18,12,40,40]`，阈值仍0.8；卡片区域按实际明细/关闭框推导。Canny内外双轮廓必须去重且两行三列、尺寸/间距一致，ambiguous不授权敌方。保存的公共流程不随候选覆盖，本轮只通过CAS迁移三条战斗公共流程的6处关闭ROI，原文件在`next/.local/giant-roi-workflow-backup`；其余用户改动保留。

- 战斗选项版本`strategy_settings_version=2`由ProfileStore统一迁移：先校验旧revision，备份原文为`strategy-settings-v1-<revision>.json`，再原子替换，重复启动不重复迁移。测试必须复制到独立目录，不直接用正式profile触发迁移验证。回滚旧候选须先停服务、保留新配置、人工恢复对应备份；不能只换旧EXE而让旧逻辑消费新频次。`consume=true`代表动作确认，不等于删行；重复动作也必须清prepared并推进epoch。

- OCR模型统一锁在`next/resources/recognition/ocr-models.json`；依赖准备下载和校验，打包前CMake生成锁必须与源一致。繁中采用PP-OCRv5 server识别模型/字典，仍用既有检测模型，不称整套v5已迁移；英文保留原模型。定位配方必须全文匹配且唯一，不能仅移除ASCII检查或把模板阈值沿用为OCR阈值。淡入帧仍可能漏字，记录原文本、框、置信分和耗时，不把置信分当准确率。已有原帧验收不等于实际发点击或整条赏金任务通过。

- 日志设置是profile顶层`logging`，由现有CAS写入并在Run定义冻结；不要放进33项旧游戏字段，也不要以浏览器localStorage作为权威。`info`默认记录每轮内存边界，`debug/trace`才开启识别匹配期内存细采样；`trace`另外记录每帧取图元数据，`off`关闭全部可选明细。输入审计、事件及异常截图不可由日志开关过滤。`diagnostics.action_timing.collected=false`表示用户未采集动作段，不能解读为零耗时或完整性能证据。

- 本轮关键修复新增的`interruption_reason`来自原`stop_if_interrupted_after`声明，不由前端或人工技能名单产生。重启后遍历全部活动pending，保护原因优先于普通菜单重选；旧发布缓存没有此标记，新运行须重新编译/发布源图，历史文件不补写。最近一次恢复事实与同窗口重启事实分别读取`context_recovery`和`application_restarted_in_window`。
- 普通Await、异常宽限期局部到期和重启前共用只读回执结算，保留原首次提交时间/epoch/预算；未知送达只有原结果命中且通道清理成功才能释放。重启阈值前只核对当前活动Await或本阶段已声明的Observe/ongoing；guard命中不代替Observe结果，异常处理器按钮消失不清父异常，子调用返回的候选出口在真实选中后清理。
- 构建必须先资源同步/配置，再`package_functional.begin_build()`，完成web及原生构建后`finish_build()`；打包检查源码及全部构建产物身份，不能在编译期间改源码。`manage_service.ps1 -Action Validate`仅预检、不读旧服务、不建数据锁或启动绑定；候选预检先于任何退出请求。旧交付清单缺少这些身份时拒绝部署，不能删校验绕过。
- Python启动Windows PowerShell 5.1可能继承PS7模块搜索路径，令`Get-FileHash`不可用；候选新预检使用.NET SHA256流计算，不修改全局模块路径。原生测试程序位于`next/build/native/Release`，构建preset为`windows-release`；`windows-x64`是configure preset，不是build preset。
- C++20的`json.value(...).items()`借用临时对象会在迭代前失效；先保存命名JSON值再取items。独立内置业务节点的未绑定handoff向调用者同名交接，根未接出口仍ExternalBlocked，不以Prepared冒充业务Completed。
- 本机Python文件symlink测试因`WinError 1314`不能创建夹具，不能据此报告应用器20/20。保留原测试错误；隔离目录junction拒绝证明只覆盖相应Windows reparse路径，不改系统权限、不改弱原断言。
- `analyze_run_timing.py --runs-root ... --output-dir ...`只读已有日志；缺文件、截断、候选身份混用或无数据均拒绝。exclusive、worker、节点墙钟不可叠加；历史`recognition.resources`事件在Session.run后但局部持有者释放前，不能当join/所有者释放后的OS内存基线。
- `worker_finishing`位于终态序列化和工作函数返回之前，不代表所有外层对象已析构。后续用`memory-lifecycle.json`区分定义释放、线程join和批次配置释放；只在开启内存且info或更详细时采集。它是终态之后的独立证据，缺失不能按零内存处理，也不得改变已冻结的diagnostics.jsonl行数。candidate66批次结束同一进程从约408降至110 MiB，仍未完成分配归因。
- `result.events`是包含`events/last_seq/resync_required`的对象，不是事件数组。UI环容量1024；`resync_required=true`只表示尾窗不完整。新候选以`execution-events.jsonl`保留全执行事件、result单独保存终态，分析器校验行数、序号和身份；旧日志没有磁盘事件流时必须报告尾窗覆盖不足。输出前先选择嵌套字段，避免展开整个事件对象。

- 连续异常计时和只读传输故障计时分别维护。60秒升级重启仅关闭绑定游戏；先记录新帧诊断，再走既有VPN/启动/Boot。网络处理器内按钮确认不清除连续异常，正常业务进展或已声明ongoing清除。不得用换节点、弹窗消失或操作已发送冒充恢复；取消与任务总墙钟不放宽。
- RunStore诊断stage只接受`reward/pre_action/postcondition/recovery_entry`。重启前截图沿用`recovery_entry`，具体用途写入`evidence_kind=before_application_restart`；不能自造stage，否则图片未保存会产生`DIAGNOSTIC_INCOMPLETE`并阻止自动续轮。历史失败保持，不通过跳过诊断准入消除问题。
- 应用重启后不能因父业务已prepare跳轮而退出会话再要求外层重跑根任务。送达已知的可重试菜单在原Call内重新选择；跳轮/普通金币住宿可用显式`retry.restart_from`指向同定义只读Route，append时必须同步命名空间。金币提交无累计3次硬上限，仍记录真实次数、5秒间隔和结果；宝石购买/送达未知保护保留。

- MuMu实例元数据必须调用nx_main/MuMuManager.exe的`info -v <index>`，不能对MuMuNxDevice.exe传info：后者是启动入口，实测会无stdout且拉起实例。管理器路径从既有绑定定位，不按可见同名进程猜实例；12.0进程不代表配置中的15.0实例2。误启动后不得用新元数据反推原退出原因。
- 游戏退出到桌面会从900×1600变为1600×900；游戏会话须在ROI前按前台包产生`GAME_NOT_FOREGROUND`读取故障，保留真实诊断像素而不替换最后有效游戏帧。只读桌面预览不受此游戏门禁限制。2026-10-01第56轮暴露的入口缺口已由candidate57修复；诊断核对前台包、视口、pending及原始像素身份，不裁剪越界ROI或降低阈值。连接代次变化与应用重启是两种独立证据，不伪造重连代次。

- Windows PowerShell 5.1按本地代码页读取无BOM的.ps1，中文注释可能造成解析错误；`manage_service.ps1`和`service_lifecycle.ps1`采用UTF-8 BOM。批处理内容为ASCII命令，不能带UTF-8 BOM，否则首行@echo被当成未知命令。5.1的Start-Process返回对象须提前取得Handle再等待短命子进程，否则ExitCode可能为空；修正后的实际生命周期检查已通过，不将测试对象读取问题误报为重复后台运行。
- 不用Windows PowerShell 5.1的Start-Process启动常驻后台：实测即使重定向stdin/stdout/stderr，仍会继承额外Shell管道句柄，后台已READY但调用批处理迟迟不返回。管理脚本调用同一EXE的`--launch`，复用原生Windows进程封装，以`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`只继承NUL输入和两条日志句柄；启动器返回PID后退出，后台独立运行。最终candidate53真实启动批处理返回0且后台存活，不能以仅看到READY代替此检查。
- 工具部署使用`next/tools/manage_service.ps1`的Start/Deploy/Stop，核对实时实例ID、数据目录、EXE、PID与进程创建时间后走正式退出接口，并等待对应进程真正结束。管理锁与数据锁分别防重复操作和重复后台；监听已关但旧实例仍清理时等待数据锁释放，不启动旁路实例。成功启动保存候选的`service-launch.json`，打包双击入口复用实际目录/端口，不误读默认LOCALAPPDATA配置。候选53已绑定正式data目录和17654；界面也提供“退出工具”。退出超时保留诊断且不启动第二后台，不按同名全局杀进程。详见`docs/reviews/service-lifecycle-20261001.md`。

- 技能详情的“明細”图标会随布局下移：2026-10-01实帧模板46×80位于y=1004到1084，ROI截至1050会误判详情已关闭，进而重复打开技能并在详情内点击Auto。共用详情控件带现为`[720,750,180,430]`，不降阈值；核对详情、当前行动者和目标箭头后，还须查实机选敌输入及施放结果，不能仅以识图Hit代替链路通过。
- 冷启动悬赏任务必须承接游戏仍在战斗/宝箱/迷宫的现场，也要承接已打开的繁中“委託清單”：先把委托页认作已知入口，再交既有公共公会开页链切到悬赏页，不能在InspectBoard只读等待。复用正式副本已定义的子流程及Return出口，避免重复展开全部策略触发节点上限；Call需绑定全部handoff。返哈肯中的encounter/stopped交回遭遇/续行处理，不接通用RecoveryRequired，否则会把正常宝箱误报为budget_exhausted。共享“關閉”按钮不证明公会页面，公会关闭步骤必须排除战斗。

- ROI与模板同宽高时只有一个匹配位置，不具有平移搜索容差。`next/tools/audit_roi.py`只读盘点作者源和原生来源行，`--workflow-root`可额外检查保存流程；32px仅为静态警告线，不是运行阈值或自动扩框规则。菜单用当前页面的候选控件带，目标/后置/补点同源；当前行动者仅在顶行检索，Boss识别才扫描完整行动条。页面身份不得绑定可滚出屏幕的控件。
- 菜单补点不使用无关背景的`region_quiet`授权；按原页面、覆盖层排除、独立新帧、原期限及间隔重新定位。运动采样、地图坐标局部判断、Pause布局统计不能按普通模板ROI扩大。不得无差别全屏重搜、静默覆盖作者ROI或降低购买保护阈值。
- 只读Observe分支从选中到执行仍可能跨帧变化（导航Stopped转战斗）。新帧NoHit须在没有pending时撤回尚未执行的选择、回原候选集，保持原分派期限；不能守在失效分支等60秒，再用通用`budget_exhausted`误导排查。超时末端只允许新帧命中同一业务图明确标记的场景，且同一场景不可反复刷新期限；普通闪烁后继不算场景进展。读取实际`execution.changed.last_diagnostic.reason`及失败帧；Operation/Await/送达未知不适用选择撤回。窄检查`test_native_flow --selection-race`及`--timeout-reclassification`，不替代实机导航验收。
- 截图回放的识别服务须使用清单闭合的隔离资源目录；直接把含manifest等额外文件的源码pack目录作为Bundle会报`RESOURCE_NOT_IN_MANIFEST`。中文资源路径使用既有`path_from_utf8`，不要将UTF-8字符串隐式转为Windows本地代码页路径。有限回放的合成位移必须标注，不冒充实机验收。
- 原生共用探针从CMake选取的资源集合冻结，不是整个作者目录；新增`resource()`引用时同步该集合并检查完整条件调用，源文件哈希匹配不能证明冻结子集包含所需资源。MSVC对单个原始字符串长度报C2026；生成目录按资源输出相邻原始字符串保持完整JSON，不删资源或改回运行时读取活动文件。
- ROI按控件的活动区域确定，不能用一次截图中的位置加紧边界。可横移的公会页签入口按完整页签栏检索；网络单/双按钮按整个按钮行检索；标题等固定锚点可以收紧。2026-09-30实际悬赏页签文字跨x=709到825，原ROI右界800导致0.4341，覆盖后0.9893；开页已成功却未获后置确认，父调用超时后停在列表。实测领取关闭回执后页签栏还会自动回到左侧，悬赏页签离屏但当前页面不变；页面身份不能依赖可滚出屏幕的页签。当前悬赏页面用专属标题、已加载WANTED卡片及委托页反证共同确认，仅标题仍不放行。检查时同时核对目标和后置配方、真实模板尺寸、动态装饰及独立新帧，不能仅降低阈值或扩大重试次数。
- 安卓VPN必须读安卓实时状态，不以电脑代理进程存在作为已开启的证据；vpn_management中默认Legacy VPN/type=-1只是占位能力，不是活动VPN。检查tun接口UP或实时VPN NetworkAgentInfo，并记录所属包；开启`AUTO_START_CLASH`后正式启动链与实例恢复链已有EnsureVpn，在已连接时保持现有VPN。用户要求开启后通过正式profile CAS保存，避免每次恢复仍用关闭的冻结配置。
- 连续读取故障默认60秒；仅绑定实例元数据已确认进程退出、进入`DEVICE_INSTANCE_RESTART_REQUIRED`/`DEVICE_INSTANCE_STARTING`时扩为180秒，再执行原实例启动、ADB、VPN和游戏恢复。单次命令或业务节点到期不代表设备已退出；未知输入保留原回执、不重发。外层恢复不再按3次硬停止，仍受任务总墙钟期限、用户停止和设备实证约束；运行中模拟器退出尚无本轮实机故障验收，不能据受控窄测宣称通过。
- Clash冷启动不使用10秒内部期限判失败：沿用120秒生命周期步骤窗口，取消和外层连续读取故障期限仍有效，失败前再查实时VPN状态。2026-10-01实例2冷启动曾在tun已建立时误报权限/配置缺失，不能据旧超时推断用户VPN没配置。
- 自然启动公告用“公告”标题与底部关闭按钮双证据，不识别日期/正文，也不靠任意关闭按钮认页。新增语义资源须同步作者目录、CMake冻结集合、resource缓存注册、启动/阻塞观察及实际PNG；用真实公告正例和标题页反例核对。
- Boot动作前后复制的ObservationPhase节点需要允许正常重复经过，max_hit=1会在第二次独立网络错误时截断启动；非输入节点用0并保留阶段时钟。网络重试不要用右下动画静止作为授权条件：已确认错误正文与重试按钮仍在就可按间隔补试，每次重新取帧、定位，首次结果期限不刷新，父级付款/跳轮等不重放。`--boot-progress`已扩展连续独立错误及背景运动补试场景；受控叶子证据不能替代自然网络故障实测。
- 住宿识别组合曾因重复嵌入完整城市反证，令 `default_dialogue` 内部模板超过 `WVD_CONDITION_DEPTH` 上限。`all/any` 会检查全部子条件，不能指望前置业务条件为 false 就跳过深层错误。修复时展开等价条件、消除重复后置条件；用实际失败帧逐一检查观察/后置/重试整棵条件，不能只测模板叶子，也不能用提高深度阈值或吞错代替修复。窄入口为 `test_native_author --inn-frame <900x1600 PNG>` 和 `--inn-transitions`（后者须设独立 `WVD_CLOSURE_ROOT`）。
- 启动阶段曾将网络重试、下载和加载累计计入120秒，下载100%仍被`OBSERVATION_PHASE_TIMEOUT:wvd.boot`截断。现为未知页面无进展窗口，已识别动作/子流程分别用自身期限，普通Poll不得刷新窗口。若用控制节点包装输入，必须保留原识别字段和按钮消失时的重新选路，否则会把正常转场锁进单一旧按钮。`--boot-progress`覆盖这三项，不连接设备。
- 繁中网络错误有居中单按钮和“返回标题＋右侧重试”双按钮两种布局。重试ROI须覆盖按钮行`[100,800,700,220]`，原右界640会裁掉右侧84像素文字而漏识别；正文和文字共同确认，点击匹配中心，不降阈值或固定点中央。原生探针、作者语义源、生成包同时更新；窄入口`--network-layouts <单按钮PNG> <双按钮PNG> <住宿确认负例PNG>`核对命中与点位。

- `package_functional.stage()`会先同步作者源并回写`next/packs/wvd`副本，不是纯输出目录复制。独立stage前后核对源/副本差异；仅恢复可证明由本次stage产生的副本改动，不覆盖作者源或用户修改。候选采用同步后的资源，身份报告需区分stage时diff与最终文档修改后的diff。
- 作者页保存包含PUT与后续目录刷新。验证保存落盘应等待写锁释放及成功提示，不能把保存按钮disabled当作完成（保存中同样disabled），否则独立核对GET可能触发既有`WORKFLOW_REPOSITORY_BUSY`。正式CAS为HTTP400加`PROFILE_CONFLICT`，不得凭通用409假设修改接口。

- ddc3366收束使用显式 `WVD_CLOSURE_ROOT` 与18754/18755两个隔离端口，正式API必须是本轮新建数据根；closure服务没有离线参数，禁止放行设备/运行POST。本轮测试替身只在C++测试目标内，UI受控响应不得透传未登记请求。端口被占用就查明归属，不连接17654或杀未知进程。
- Windows资源缓存路径含完整哈希和UUID，应使用短的独立验收根。过长的根目录叠加长素材名会触发MAX_PATH错误，不能把它当缺素材、删素材或绕过校验。本轮根为 `next/.local/cl-ddc-927`。正式构建仍沿锁定依赖，旧失败证据不得覆盖成成功结果。
- Profile顶层是既有33字段契约，历史未知值按现有扩展/legacy_passthrough保留；不能往values顶层添加任意测试字段后要求API接受。任务专用覆盖只有全局启用时生效。CAS用例必须提交字段合法、引用完整而revision过期的文档，避免先触发引用保护而误判CAS失效。
- 时序替身调用正式执行器时仍须使用合法结构化source_path及正式公共步骤作用域；不能把任意字符串路径或纯结束节点作为可提取业务块。布局验证和真实识图验证分开，受控叶子观察不证明真实模板匹配率。

- 转场审核不能只看输入的next是否写了Done：执行器进入无条件Route后不会回查父级。输入后置接受的每类页面都须在后继闭包中持续可达；终点在点击后和移动/加载期间都要可达。跳轮提交后的轮盘残帧不得结清输入或重回选章；普通菜单与剧情同页重新观察不等同业务成功，不能机械统一成“页面必须消失”。

- 2026-09-27用户明确区分货币：G是普通金币，既定任务中几百/几千G的支出和少量重复恢复成本可接受；禁止自动点击关联绿色或紫色宝石的“購買”。已接入住宿实际提交账目及有限补试，送达未知只核对结果。不扩展到卖装备、抽卡、任意购买或无限重复扣款。历史“付款全部不可重复”不能充当最新需求。

- 动作耗时扩展见`docs/reviews/action-timing-20260927.md`。原256条事件窗口会淘汰早期事件，不是完整时间线；新候选每轮另由同一RunStore保存`action-timing.jsonl`，64MiB上限及丢失/失败计数明确落盘。核对实际EXE后才能声称记录已启用，不能把新源码功能套到仍在运行的旧候选。
- 工作台循环模式为一直循环/指定次数，API为`repeat:true`及可选`repeat_count`。更新候选不会热替换后台；不可在付款或未决输入中强杀服务。浏览器自动化曾出现截图坐标偏移导致click落到邻近控件，可对已确认语义按钮使用键盘Enter，并核对API结果；工具返回click成功不等于页面发起了请求，不要误存偏移操作改动的配置。
- `/api/v1/device/connect`需要模拟器路径、实例、ADB地址等请求字段，应读取当前profile组装，不能发送空对象后把缺path异常当设备连接失败；任务启动入口会从冻结profile自行连接。部署后从郊外独立启动曾停在Boot，不可仅因任务内部有InspectOutside就声称启动入口接受郊外，必须区分前置检查和后续业务。
- 事件priority有效区间为0–1000，不能靠1100抢优先级绕过校验。MSVC C++20下JSON对象与字符串直接比较可能触发重载歧义，按实际字段类型用`get<std::string>()`后比较。

- 每轮先界定唯一用户功能和最短验证路径。构建、Mock、离线流程、接口 2xx 均不等于真实游戏任务成功；不跑旧全任务矩阵、资源长测或重复失败用例挑成功。实机副作用必须遵守当轮明确范围。
- `test_native_author`的窄识图入口使用相对路径`resources/authoring`及`packs/wvd`，必须从`next`目录执行；从`next/resources`运行会读空JSON并报parse_error，不是图片或生产配方损坏。真实负例必须核对画面类别，不能把其它页面传入后声称已验证委托页。
- Windows 中文文件与输出使用 UTF-8。长构建日志写 `next/.local/logs`，只检查退出码和关键摘要；不要按截断输出下结论。
- `git diff --check`沿用仓库换行配置并将警告写日志。不要为消除LF/CRLF提示临时设置`core.autocrlf=false`：这会把既有CRLF资源文件整份当作差异并产生大量伪尾空白报告；无需改写这些用户文件。
- Git 推送若报本地代理连接拒绝，先核对 `git config --show-origin` 的代理及当前可用监听；全局旧代理可能覆盖进程环境中的新代理。仅对当次命令用 `git -c http.proxy=<已确认的当前代理> push fork ...`，不擅自改全局 Git 配置或切换游戏 VPN。直连也可能不可达，不持续挂起推送进程。
- 新版完整构建入口是 `python next/tools/build.py`；本轮只需受影响的 CMake Release、Vue 类型/构建、资源一致性和候选整理。`validate.py` 是旧大验证入口，不用于本轮修复收口。Maa 准备脚本和 M2/M3/M4 旧验收已归档。
- c11d1a1 工作包要求仅构建受影响目标和限定实机正常路径，不跑 ctest/pytest/旧矩阵。当前 FlowProgram schema 7：程序共享不可变所有权，辅助调用者须在修改测试图后重新显式封存，不能恢复生产值复制接口。技能性能的主线程 exclusive 可相加，worker_exclusive 不可与墙钟相加；普通内存日志限频后不能与旧高频峰值直接比较。
- 累计预算只能来自工厂显式参数或作者 execution.time_limit_ms；PipelineCompiler 无预算参数构造的默认60秒仅是节点等待上限，不能强加给整场战斗/自动导航。已确认误转为调用累计期限会在战后检查点造成 FLOW_INVOCATION_TIMEOUT，使用 declared_budget 区分来源，未声明时继承现有会话上限。
- 选择输入分支之后可能转场：尚未提交的 scene/target/guard NoHit 应回到原选择点，保留原期限与事件暂停，不能固定等待已经消失的按钮。已提交或送达未知禁止此路径。启动页的 Ready 也不等于城市可操作：普通剧情继续与选项对话是不同配方，使用既有 AUTO/箭头反证和继续节点；不得因 Ready 步骤出现就声称剧情已处理。
- CMake 通过 PATH 或 VS 2022 的 vswhere 定位；Node/npm 版本按 `next/dependencies.lock.json`。OpenCV、ORT、scrcpy 文件按 `next/native-dependencies.lock.json` 校验。系统 PATH 中旧 ORT 可能抢先加载，构建与测试可执行文件旁必须放锁定 DLL。
- Windows 平台头会定义 `min/max` 宏；含 OpenCV/`std::numeric_limits` 的新平台头须在引入 `<windows.h>` 前定义 `NOMINMAX`。本仓库可执行目标名为 `automationd`，不是打包目录名 `wvd-next-native`；有限执行器检查目标是`test_native_flow`，不是源码文件名`test_flow_executor`；误用目标会报 `MSB1009`。
- 将全新 MSVC 构建目录放在 `%TEMP%` 会出现 `MSB8029` 增量构建警告；本轮 Release 仍通过。常规增量构建使用 `next/build`，独立验证另建目录并保留完整日志，不把该警告误判成编译失败。
- 不覆盖旧 `src/`、`config.json`、`mod`、日志、`dist/wvd`、`next/dist/wvd-next` 或 `.vscode`。原生候选使用独立 `next/dist/wvd-next-native`；调试应用一律显式传隔离 `--data-root`。
- 不要在正在运行的本目录 `automationd.exe` 上重新链接；按进程路径、命令行和端口确认后正常停止，不结束全局同名进程。构建完成后核对候选 exe 和依赖来自当前锁定归档。
- 增量构建开始后不再编辑本次编译的头文件/源码。并行编辑会产生旧签名对象与新签名对象混用的链接失败或短暂文件锁；先结束构建，完成接口修改，再重新构建受影响目标，不添加旧签名别名绕过。
- RunStore结果中的`events`是带`events`数组的包装对象。先核对必要字段，再按`result.events.events`定位序号/类型；不能把包装对象整体展开当作尾部事件，避免输出整轮私有数据。
- `POST /api/v1/device/capture`异步返回。须等待设备`operation.name=capture/state=completed`后再读`/device/frame`，否则可能读取上一次缓存截图；运行中人工抓帧被拒绝时用实际最近帧，不能把旧缓存当现场。流程保存也须等待成功状态后再运行，仍在写锁内可能返回`WORKFLOW_REPOSITORY_BUSY`，不能当游戏故障。
- 停机调查必须区分15秒采样历史帧与当前新截图；历史最后王城帧不能证明后来未进郊外。旧候选ADB_TIMEOUT未记录具体命令，仅能支持前台查询嫌疑；当前读取故障已补命令诊断与失败保存调用，需等自然故障核验。诊断summary.complete=true且failure_attempts=0不等于已保存异常截图，不能把接口存在当本轮取证已触发。
- 正常页面连续出现不等于连续失败。非输入节点不设隐式经过上限；输入连续计数仅在新帧结果确认后结束，作者显式repeat_limit保留业务循环语义。不能把付款pending、事件返回或纯Route当作成功，更不能以提高任意次数掩盖计数归属错误。
- 作者流程已有必填、正数的 `execution.time_limit_ms` 作为调用期限，不能强迫循环上的每个观察/路由节点再声明经过次数；明确声明的业务 repeat_limit 仍生效。悬赏展示卡是可选联网插曲，可能在列表出现之后才到达，需接入开页与返回途中交接。列表标题不等于内容加载完毕，原页面仍在不等于返回成功。
- Windows 刚复制的 EXE/DLL 可能使 staging 目录重命名短暂报 `WinError 5`；版本检查执行同哈希的构建目录 EXE，暂存目录只负责复制/校验。替换重命名仅有限等待重试，超限则回滚报错；先核对自有进程和原候选，不删除用户目录。
- `next/.local` 包含大量候选运行产物；不要对其整体执行递归 `rg --files`，实机证据按已知候选的 `data/runs/<instance>/<run>` 精确读取，避免无关的长时间枚举。
- 旧 Python 源码只读参考，不能 import 旧 `utils`/GUI 做 Smoke；它可能初始化日志或保存用户配置。历史 Maa 源码和证据保留在 `next/archive/`、`docs/archive/`，不参与当前编译。
- 模板识别必须区分 Hit/NoHit/Error。OCR 当前锁定英文模型，不冒充繁中识别。图标点击中心只定位入口，不能用通用城市图标推断王城；王城塔楼背景是只读地点证据。
- 菜单素材必须取淡入结束的稳定帧，再在另一张稳定帧核对。旅店“離開”曾从淡入阶段裁图，稳定画面分数仅0.76；重取稳定素材后独立帧0.996。不能靠降低阈值补偿坏素材。时序条件在同一帧的选路与输入复核必须返回一致结果，且不能仅靠重复读取旧帧累积静止时间。
- 多个候选在同帧调用识别时，不能每个条件重建并丢弃所有模板叶子证据；模板复用须保持帧身份、ROI、语言、参数和有界内存。高等级技能不能在等级条未命中时放行默认等级，点击后须确认对应按钮选中态。滚动历史截图限频不代表实际取帧周期。
- 语言排除的NoHit不是像素测量，不含`best_score/best_box`；同帧缓存换阈值时须保留排除结果，不能无条件读取分数或补假分数。candidate65首次实跑已暴露该消费者缺口，candidate66修复；对应定向检查必须包括同帧不同阈值，单模板/每次换帧检查不足以覆盖。
- 大地图多个地点复用同类建筑图标，地点身份只能由名称证实；繁中地图还需以“關閉”及缩放 `+` 共同确认场景。名称模板的中心不是建筑点击点，不能直接替换旧 `City_*` 输入目标。
- `next/resources/authoring/{semantic-assets,public-flows}.json` 是编辑源，运行时加载打包副本；`package_functional.py` 同步两份JSON及语义目录实际引用、位于`next/resources/images`的扩展源素材，并更新manifest成员哈希，随后校验繁中引用。既有旧素材没有该源时保持原包，不扫描日志/mod。发布后不得从活动作者源热读配方。语义资源的只读证据不可用作点击目标。准备阶段缺PNG会拒绝运行，HTTP请求已接受不等于任务已启动；须核对最终run状态和准备错误。
- 改动公共作者流程的节点时，同步检查 `interface.parameters[].bindings`、边与布局引用；删掉旧节点但保留参数绑定会在实际编译时报 `FLOW_BINDING_NODE_MISSING`。打包后，已有用户流程须经正式 API 检查 `update_available` 并以版本校验同步，不能只更新内置 JSON。
- 构建期生成的原生动态探针带语义源 SHA256；打包会与当前源目录校验，若只改 `semantic-assets.json` 未重新配置/构建则拒绝发布，不允许 EXE 探针与资源包各用一版。
- 原生语义目录由`next/native/CMakeLists.txt`从作者源完整生成，同时提供模板语言索引；`vision/location_probes.hpp`的resource缓存仍需注册实际使用的ID及语言。新增C++配方引用时不能只改JSON；漏缓存注册可能报`COMPILE_IMAGE_SCAN:...invalid map<K, T> key`。语言分类必须核对图片内容：NEXT/Pause/Auto及无文字图标可能跨语言共用；不按英文文件名或`_zh_hant`后缀猜语言，不将未分类mod自动禁用。改目录须同步资源、重新configure/build再打包。
- 游戏启动/VPN 需分别观察前后状态，不要求一次点击两秒生效。NoHit、黑帧、缺资源和网络慢不能升级为模拟器重启。已发送但结果不确定的非幂等输入不得重发；但打开菜单/离店这类明确授权的无资源副作用动作应在新帧重认原场景与按钮、排除阻塞后间隔重试，不能照搬一次点击后一直等死。重试保留首次结果期限；提示小ROI变化仅暂缓点击，不可据此认定加载成功或业务完成。停止只释放本工具自有输入/进程。
- 郊外归还城市可能随机触发队友普通剧情。转场后置条件需接受剧情并交接继续箭头处理，不能只等城市菜单、不能因背景像王城提前宣布完成，也不能超时后重放归还。
- 正常业务不全量扫描异常：内置网络/异常/特殊规则在当前结果连续未确认后分派，包括输入结果等待；动画去抖/诊断限频 1 秒不是点击生效期限。繁中正文和“重試”按钮共同确认，英文保留独立模板；复用原生事件暂停父等待，退出后检查原后置条件，不重放提交/付款/跳轮。网络处理器总预算 180 秒，按钮消失等待最多 120 秒，再次出现按新帧重试；未知错误/维护页不得借通用确认按钮强行继续。
- PipelineCompiler 的 finish/refresh_images 会校验可达闭包；先建立处理器的正式引用，再 finish，不能将不可达处理器留到封存后才接线。检查输入在途事件时用输入状态表达场景，不依赖进入 Await 前存在多余截图的固定帧数。
- 蝎女连续循环由 Application 管理，在工作台勾选“连续循环”后开始；停止按钮调用同一服务的 `runs/current/stop`，取消续轮并停止当前轮，轮间同样可停。命令入口 `next/tools/run_scorpion_loop.ps1 -BaseUrl <当前服务地址>` 只提交 `repeat:true`，不再启动外部守护进程、不读写 STOP。旧日志/停止文件保持原样，不参与新会话控制。滚动历史帧及异常PNG仍由正式RunStore保存；已运行旧脚本必须先停止，不能与后台会话并行调度。2026-09-27用户要求补回崩溃恢复：失败后由同一协调器观察设备，确认绑定实例退出才重新打开该实例，ADB离线只重连；保留原任务业务状态，恢复失败或未决业务副作用仍明确停下。
- MuMu崩溃后的`info`可能返回`error_code=900/901`，但仍带完整实例身份及`is_process_started=false/is_android_started=false`。这是可核实的退出状态，不应在启动前按普通错误拒绝；缺字段、身份变化、其它错误码不得当成退出。运行中的实例/Android启动中不满足自动重开条件。使用配置的MuMuNxDevice入口显示窗口，不能改用会启动后台实例的管理器restart命令。
- scrcpy的远端jar未删除与输入未释放必须区分；ADB离线无法rm静态文件，不能因此永久卡住任务准入。实例确实退出后，可撤销该实例的旧按键持有状态；本地子进程退出和自有转发消失仍需确认，业务输入是否生效的未知记录不能被清除。
- `INPUT_CONTEXT_CHANGED`是门禁在底层发送前拒绝旧帧，不是送达未知：丢弃旧证据、使设备元数据缓存失效，再沿原选择节点重新取帧，保留原期限；菜单重试期间保留既有pending。其它拒绝仍失败，已发送/未知输入不适用。已按此规则恢复并真实Completed的轮次可续轮，不能再用累计rejected非零否决最终业务回执；拒绝仍如实计入日志。
- 蝎女普通路线已有正式 API 完整循环证据；更换 EXE 后必须记录新候选身份及本轮实际范围，不能沿用旧 PASS。其它任务、随机事件和长期稳定性不能由普通一轮外推。实机范围由用户当轮指定，不自行扩展领奖、购买、复活、额外跳轮或刷怪。
