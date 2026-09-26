# c1666a0 关键修复工作包收口

## 范围与状态

用户要求完成 WVD_c1666a0_Key_Repairs_20260926 工作包。基线 c1666a02b348e1641b8ab90ac98d024ea43dd824；验收时保留未提交差异，未自动 commit/push，随后用户单独授权提交并推送个人仓库。旧 Python、config.json、mod、日志与 .vscode 未覆盖。无全量测试、长资源测试、故障注入或额外任务矩阵。

K01-K09 已按包内基线哈希核对并应用；R01-R05 已接入唯一原生链。最终构建、同一候选实机、停止和结果证据由下方验收记录及 JSON 索引给出，不把组件成功替代游戏成功。

## 实现

| 项目 | 实现与责任边界 |
| --- | --- |
| K01 / R01 程序所有权 | Application 一次发布后封存 shared_ptr<const FlowProgram>，三个 NativeUnit 共享一图；RunDefinition 禁复制。Session/Call 独立持有计数、回执和事件栈。辅助调用者改为显式封存，不恢复值复制接口。 |
| K02 / R01 异常结果 | Session 在任何富结果构造前设清理责任，先保存未决 bool 并关闭/回收输入，再生成明细；失败保留 details_complete=false。API、历史、终态均明确该字段，缺字段历史只读显示未知。WORKER_ABORT 只兜底，不伪造 Completed，不代替 storage_error。 |
| K03 / R03 观察周期 | 唯一 FlowExecutor 持当前不可变帧及覆盖层 NoHit 的有效作用域。纯控制复用，输入、等待轮询、事件返回、身份/TTL失效后重新取帧。事件轮询不被长控制链饿死，不延长 TTL，不缓存 Error/歧义/退出等待为安全。 |
| R03 技能索引 | 纯业务 guard 由游戏层 BusinessPredicate 读取已准备索引，不能发输入或消费技能。覆盖层优先；真实点击继续检查角色、场景、目标和 InputGate。mixed all/any 原 Error 传播未改。 |
| K04 设备事务 | 一次 ADB 客户端执行 window → input → window，验证三段唯一标记、各段 RC、前后台、viewport/rotation/代次及取消。响应总8MiB、各段2MiB，不退回多客户端旧链。像素采集时间与元数据完成时间独立。 |
| K05 缓存维护 | trim 用 iterator 删除，noexcept 路径不组装 key，不调用 at；维护异常标记资源错误，不把非法缓存继续当可用。evictable 为标量计算，Lease 保持不可变像素借用范围。 |
| K06 对话策略 | Application 冻结策略后传给识别绑定，bound_dialogue_task 使用冻结名；素材依赖枚举沿用同一策略。 |
| K07 模板复用 | parallel worker 同步期间只读父 memo、保存局部新增；单最佳模板缓存 score/box，阈值变化重新计算 Hit/NoHit。ROI、缩放、预处理、遮罩、scope、语言和资源版本继续参与身份；multiple 不进入该缓存。 |
| K08 资源诊断 | 固定槽16；普通 OS采样/写入至多每秒一次；资源失败立即新采样并写记录。限频后采样峰值不能与旧高频峰值强比。 |
| K09 / R04 预算 | 网络 Begin/EndObservationPhase 总180秒，内部重试入口不重置。schema4 Definition 保存显式声明累计预算，每次 Call 一次签发；事件暂停父有效预算，Run墙钟不暂停。默认节点60秒不冒充调用预算；未声明则继承现有会话上限。 |
| R02 分项耗时 | steady_clock 固定数组；主线程嵌套扣子段，并行worker单列。会话及确认技能保存采集/转换、元数据、输入复核、准入等待、真实match、并行等待、JSON事件、显式等待、辅助提交、未归因余量和次数。旧缓存 timing_ms 不用于新计算统计。 |
| R05 历史帧 | RunStore owns jthread，pending/in-flight各一帧，共享不可变BGR；生产者try-lock到期提交。JPEG、目录工作不在任务线程；满槽丢辅助图计数。结束关闭并join，不持诊断锁join，不detach；业务/故障/终态不进入丢弃槽。 |

## 实机暴露的预算接线错误

首候选 dd626725586d9137c52921a3fb8a39f9e8a659156ffca391d443066894d7dc43 实际执行了跳轮、悬赏刷新、B2F蝎女战斗及 Skill17Success；22次底层输入、0拒绝，未重启模拟器。随后以 FLOW_INVOCATION_TIMEOUT:Task_FirstDungeon_Battle_Entry 失败，quiescent=true/result_saved=true/details_complete=true。不能计作整轮成功。

这是本轮 R04 接线错误：把构建器未显式传预算时的默认节点60秒，错误保存成了整个 Battle Definition 的累计期限。原源并未声明整场战斗必须60秒内完成。已加 declared_budget 区分无参数构造与显式参数/作者声明；append/define_child 只传递已声明值，默认战斗/自动导航累计预算留空，继承原会话上限。没有扩大等待数字或反复原样重跑。

首候选真实技能区间31.4421132秒，其中真实串行match 23.8385848秒、截帧元数据3.3839084秒、并行等待1.8794766秒、输入复核0.592282秒、显式等待0.4534264秒、未归因0.0138282秒；1155次真实match、513次叶缓存命中、28个ADB客户端、2个输入前事务、22次取帧。此技能目标为Lv5，正式图的 LevelSelected guard 明确 selected=true，业务 success 已消费。原21.6秒不是本轮对照，不宣称加速成功。诊断显示取图本身不是主要耗时，后续减少真实匹配须另设明确范围，不能靠改阈值或绕过反证冒充优化。

失败证据在本机 runs/E5870061-3194-433D-B8C1-28973BABBFA6/1/result.json；最近帧保存19张、失败/丢帧0，最后两张为 recent-frames/20260926T084834366_r1_f181.jpg 与 20260926T084850063_r1_f192.jpg。没有单独故障PNG，不冒称已保存。原始资源/程序图/截图不上传个人配置或机器路径。

## 最终验收

K01-K09、R01-R05 已实现并完成正式 Release/CaptureHost 构建；辅助协调器入口已编译，不执行旧测试 EXE。前端未变，不重跑 Vue。R06 已在最终同一 EXE 完成有限实机和停止收口。仅该工作包及以下普通路线通过，不代表资源稳定、全部任务或任意阻塞取消通过。无全量 ctest/pytest、旧矩阵、故障注入或长资源测试。

最终 EXE：69210c1440e843fa527db452e6e73e5b640a81b74ffc63af202b3f2845ddc123；工作树基线 c1666a0，构建差异身份 363a71307852b676cd67216628d2555bd20872d1685abc547192f20b43054580。候选为 next/dist/wvd-next-native，验收服务 http://127.0.0.1:17652；验收时未 commit/push。报告收口和用户授权的源码提交发生在构建后，构建差异身份不改写为后来的文档差异或提交身份；40 项原生源码/模块说明独立哈希在 [证据索引](key-repairs-evidence-20260926.json) 中。

静态包 revision d1fa6662612dfd51ea50f3b31d4a3f31515f8e2fc38a74611cad867f434d9197；语义目录 SHA256 7aa5d2aa9fe22a1b219325fcc4e815f6d779370f9e5a26df6c8781d6722dd153。Application 发布的本轮程序/资源快照 revision 8e660377551066cda8df0e92e6ae9a3d6b9fe656d3c85f4594d9d035c182b0eb，两者是不同对象的身份，不互相冒充。schema4，40 个 Definition、5907 个业务图步骤（不是5907条测试）；三段 owners=[0,0,0]，唯一程序对象1，运行代次1/2/3各自 Completed。

最终实例 C445C09B-5DD6-4432-8F98-1E459319611B：

- 运行1：自然普通剧情 Boot_Story 的 input.observed 已确认；随后正式停止约232毫秒进入 UserStopped，1次已确认输入、0拒绝、cleanup=1、pending=0，quiescent/result_saved/details_complete=true。该停止计时包含 HTTP/100毫秒轮询，不是微秒级底层精确取消上界。
- 运行2：正式 API、繁中 Scorpionesses、法术+地裂方案，完成旧报告、200G住宿、矿石跳轮、悬赏刷新、初始奈落 B2F 导航/战斗、恢复、快捷返哈肯、返城、本轮报告及200G住宿。Completed，completed_business_units=3，completed_cycles=1，reports_remaining=0，inn_rest_completed=true，61次底层输入、0拒绝、cleanup=3，quiescent/result_saved/details_complete=true。最终现场为王城，无人工补点击、无模拟器/VPN重启，无自动续跑。
- 返程真实步骤 Choose@await → Resume → Choose@await → Retreated → Terminal，证明本次在 Resume 提交前转场后重新选择原分支，而非重发导航输入。普通住宿剧情也实际经过 ContinueStory/ContinueSupply，再回城。没有外部事件栈的自然网络/维护场景；不把普通剧情节点等同所有事件嵌套验证。
- 最近帧保存50张、失败0、丢帧0；pending/in-flight各1槽，JPEG/目录工作合计 worker 1.3701217秒，终态前已join。必要故障PNG与辅助JPEG不是同一记录；前两次失败原结果没有故障PNG，报告保留这一缺口，没有伪造文件。

### 最终技能实测

Skill17Try0LevelSelected 的正式 guard 为 skill_level=5、selected=true；真实步骤已到 LevelSelected 和 Skill17Success，业务 consumed=true。区间从 Prepare 到确认成功，33.6801344秒。

| 主线程独占分类 | 秒 |
| --- | ---: |
| 真实串行match | 25.2539620 |
| 截帧元数据 | 3.4873176 |
| 并行结果等待 | 2.2379018 |
| 其它识别处理 | 1.3421667 |
| 输入前复核 | 0.5667566 |
| 显式等待 | 0.4545159 |
| 像素采集 | 0.2710510 |
| 颜色转换 | 0.0428957 |
| JSON/事件 | 0.0020922 |
| 送达/预算等待/辅助提交合计 | 0.0004801 |
| 未归因余量 | 0.0209948 |

上述独占合计33.6591396秒，加余量等于墙钟。worker match 2.4171678秒、准入等待1.1511859秒单列，不再加到墙钟。1155次真实match、513次叶缓存命中、28个ADB客户端、2个输入前事务、22次取帧、20次覆盖层/帧复用、20次纯业务判断。真实match占技能墙钟约74.98%；截图采集本身不是主瓶颈。

完整一轮776.4849113秒（约12分56秒），各会话墙钟252.3251825/360.9593636/162.4471792秒；串行match合计500.8266694秒。没有同负载对照，不能把本轮33.68秒与旧21.6秒包装为改善，更不能称工具已经够快。进一步减少高数量全帧/反证匹配属于仍需明确范围的性能工作，不用降低阈值、删反证或额外离线矩阵代替。

### 资源与未观察项

三次会话末采样的解码缓存 live/retained=4,306,878 / 4,729,665 / 3,145,635字节，in_use=0，maintenance_failed=false；并发峰值2，estimated workspace峰值247,416,756字节（估算，不是已捕获分配栈）；末次private=414,298,112字节，系统commit=7,785,788页、limit=13,874,604页，页4096字节。Program对象1与JSON程序文件56,997,726字节分别记录，不把文件长度当驻留内存。系统commit不是本进程占用；稀疏采样与旧高频峰值不做泄漏比较。最终王城原图保存在 next/.local/repair-c1666a0-20260926/final-city-20260926.png，哈希在证据索引中；不作为新识别素材。

RESOURCE_UNRESOLVED 保留；网络覆盖层退出、维护、专用对话选择、诊断分配失败、磁盘永久阻塞均未自然触发或未验证。普通剧情继续和本次有限停止通过，不外推任意原生阻塞可取消。源差异、EXE、静态包、发布图、原结果哈希和分项重算见 JSON 索引；原截图/事件/设备输出保留本机，不上传个人配置或机器路径。

### 返程实机暴露的未提交分支失效

候选 1dbe4a5d8d1688a76164ae84b2f9d9282b16463cf32bc1bc193e0d997c31e0be 的普通链完成跳轮、Lv5 Skill17Success、战后恢复，并实际到达哈肯。随后 navigation.auto_route.budget_exhausted 失败；44 次输入、0 拒绝、清理 2 次，quiescent/result_saved/details_complete 均 true，未继续返城/交报告。此轮不能计作整轮成功。

现场正式识别探针确认哈肯两项模板 Hit，分数 0.99998146/1.0，ROI 未调整；现场图在 next/.local/repair-c1666a0-20260926/navigation-harken-20260926.png。末步为 Choose@await → Resume → RecoveryRequired；Resume 没有提交新输入。分支在移动画面中选中，执行输入前已经进入哈肯；场景或按钮 NoHit 后只等待旧节点，未重新选择 Retreated。

FlowExecutor 现记录多分支选中的未提交 Input 原选择点。仅在尚无 pending、场景/按钮/guard NoHit 时撤销该选择，退还尚未执行的 hit 并重新选择；原选择期限、调用期限、事件暂停时钟保留。输入提交后 advance 清除选择来源，拒绝/送达未知不经过重新选择路径；Error 仍失败。事件 replan、错误路由清除来源。不是重发已送达动作，也不是扩展点击预算。候选再构建后有限复核，不原样刷失败直到偶然通过。

### 自然触发的启动普通剧情缺口

人工恢复王城时触发莉莉丝普通剧情。6d6e7c3b59f02e4115bfac35822f16aae45d635afd768bba6e9640c72363fe70 从此页启动后，进入 Ready 和公会入口却未点剧情；已停止该运行。default_dialogue 的含义是已知选项页，不能冒称涵盖普通继续页。现场新帧 default_dialogue=NoHit/no_default_option 与可见 AUTO/继续箭头一致。

启动/通用恢复复用既有 ordinary_story_page 和 story_advance_arrow，排除已知选项页；Ready 加普通剧情反证，Story 沿用原累计期限、2秒间隔、12次上限，无新素材、不改变专用策略。最终 69210c1440e843fa527db452e6e73e5b640a81b74ffc63af202b3f2845ddc123 的运行1实际经过 Boot_Story → Boot_Story@await → Boot_Entry，input.observed 确认箭头，pending=0；停止后 NotCompleted/UserStopped，不能当作根任务成功。自然剧情已观察范围只限这一普通继续页，不宣称全部选项/事件恢复通过。

## 长期边界与恢复

历史 OpenCV OOM 仍为 RESOURCE_UNRESOLVED。程序对象数、缓存、workspace、进程private与系统commit分别记录；没有现场分配栈，不认定泄漏消失。网络/剧情/维护未自然触发则 NOT_OBSERVED，不额外刷怪制造异常。外部原生等待/磁盘永久阻塞的任意取消能力不由本轮外推。

回滚依赖版本控制及本次源差异清单：先停止自有候选、确认输入静止，再人工恢复所列源码至基线并重建独立候选；不并行加载新旧引擎、不自动回退、不回写或删除用户配置/历史。包内K变更原文件备份保留在 next/.local/repair-c1666a0-20260926/backup，未更改文件以Git基线恢复。报告、滚动事实和候选元数据区分源码、构建、实机及未观察事实。
