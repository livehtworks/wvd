# M4.8 PNG诊断接线

## 当前事实

`007a7d6`统一构建通过，8方法全部通过，不能将有限诊断验证扩大为完整任务通过。
诊断变更不替换state/boot/handoff/workflow业务；不写旧config/resources/mod/logs。

- Context仅提交本次Custom的首/末实际FrameEnvelope，保留身份而不建立图片仓库。
  GatewayHooks经ExecutionSession固定绑定本Run的RunStore；无后台线程、无队列。
- RunStore专属mutex串行处理限频、稳定operation防重、配额、PNG验证和原子写。
  写盘不占业务/门禁/协调器状态锁；回执在store返回释放锁后才发EventJournal。
- 默认128奖励+32故障、每张8MiB、总预留1280MiB；失败/tmp同样占预算。
  70次奖励不会被32张故障限额截断；所有鱼获都触发，矿物保留全改及未知分类。
- 普通reason60秒、pause120秒，复用可注入单调时钟，跨恢复不清限频；
  每个奖励operation只尝试一次，不因图片失败重新提交业务或重发输入。
- PNG检查签名、固定IHDR尺寸和真实Maa解码，不把任意encoded_image改后缀当PNG。
  数字相对路径由store生成；各级目录持有拒绝写/删除共享的句柄，拒绝reparse和目录替换。
  成功原子提交后才有saved/path/hash。失败没有成功引用，tmp原样保留。
- result.json仍沿用原schema1读取，在diagnostics.schema1附加有界索引及汇总。
  业务事实/原reason不因诊断失败而改变；diagnostics.complete独立于result_saved。

## 触发语义

GuardedAction异常先展开撤销许可并关门，保存已有前置帧或最后后置帧后重抛原异常。
未取得图片时只记诊断不可用，不截图补证。WvdConfirm在业务锁外、accepted且applied后
保存实际领取页，重复确认不重复图片；保存的不是关闭后的画面。

RequireRecovery先保留恢复reason，在recovery标记/关门前单次capture；随后关门并存图。
底层Controller截图异常可能先调用fail，所以不能只在外层catch后才设置原reason。
图片标记recovery_entry，不冒充导致恢复的原始帧；leap.wait_boundary不捕获、不存图。
取消已经发生则不追加截图，截图失败保留独立错误且不重开门、不重试。

## 测试资产

新增tests/native/test_m4_diagnostics.cpp、tests/m4/test_diagnostics.py，8方法：

1. 70张真实RunStore写入及SDK PNG解码、60/120秒边界、防重、错Run/代次、非PNG/尺寸/字节上限/时钟倒退。
2. 奖励与故障配额独立、有界索引/预留字节和超限可见。
3. 原子rename失败、tmp保留、无saved伪回执、重复operation不重试。
4. 两端都在隔离根的junction负例，不向目标目录写入诊断图片。
5. 真实Maa GuardedAction前置目标失败，已有帧PNG与像素/hash一致、零输入。
6. 真实输入后后置超时，保存最后实际后置帧、仅一次输入、不补截图。
7. RequireRecovery成功/截图失败/纯时间边界，核对原reason、截图次数、静止及保存。
8. 正式WvdConfirm/状态/视觉/发布器，领取页确认及重复确认后一次关闭，保存一次实际奖励帧。

70张是生产存储+SDK边界验证，不冒充70次完整钓鱼。奖励专项是有限领取诊断链，
不代表完整钓鱼任务/转交70份通过。测试故障来自隔离离线后端和临时目录，
生产存储、SDK、GuardedAction及WvdConfirm没有Mock替代。

## 构建接入

所有生产改动均在既有编译文件，无新生产cpp需要加入CMake。
native/maafw/CMakeLists.txt现有M4测试foreach列表已加入diagnostics：
test_m4_diagnostics链接wvd_business和wvd_vision，沿用UTF8/manifest/m4输出目录。
专属Python入口为next/tests/m4/test_diagnostics.py，沿用当前验证工具的发现方式。
早期correction1冻结产物不包含本轮改动，不能用于本项证据。

## 实际结果

`m4-007a7d6-public-state-diagnostics.log`中本组8方法通过，私有根
`m4-diagnostics-_tbkfbim`。11份执行身份与007a7d6构建的EXE匹配；10份保存结果一致，
83张已保存PNG的哈希与大小经独立复核。前/后置失败保留实际帧，领取确认只存一次，
恢复入口截图失败不覆盖业务首因，配额/节流/原子写失败及真实隔离junction负例均符合断言。
同一产物M2 104方法回归通过；不是生产环境长时稳定或任意原生阻塞取消证明。

## 未证明边界

没有证明磁盘flush、SDK截图或任意原生阻塞可取消。诊断在回调内同步执行，
遇阻塞时必须继续保持Session/Run/store和租约所有权，不能用外部watchdog当生产取消。
没有执行任何真实设备操作；后续修改存储/停止路径仍须相应回归。
