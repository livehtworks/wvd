# MuMu迁移F盘核对与完成记录

本轮开始于2026-10-30，完成于2026-10-31（Asia/Shanghai）。用户授权把MuMu迁至F盘，保留原三个实例与游戏数据，验证以后新建实例也在F盘，验证后关闭模拟器；不启动循环。当前结果为**迁移完成，原三个实例、管理器及正式/隔离WVD服务均已关闭，监控PAUSED**。

## 当前安装与实例

唯一安装根为`F:/Applications/MuMuPlayer`，原`C:/soft/MuMu Player 12`已由用户通过原生卸载器移除，未建立C到F的链接。主程序、两个引擎、三实例与实际新建目录均在F盘。F为NTFS HDD；用户已接受启动可能变慢。F盘Program Files受保护，因此使用统一的Applications目录，没有改ACL。

产品仍为6.8.8.0，main1.8.8.5891；安卓12为12.8.8.5892、ROM120.2.183.005、renderer0.0.316；安卓15为15.8.8.5893、ROM152.0.50.004、renderer1.0.101。实际模块版本与原安装一致，六个核心EXE在安装后及验收后SHA均与原C盘相同。MuMuRemoteService及卸载登记指向F盘；Windows共享驱动/COM运行时仍由同版本安装器放在其系统位置，未手工搬驱动或修改版本、渲染、CPU/内存、VPN配置。

| 原编号 | 安卓 | 名称 | 原data.vdi字节 | 本轮验证 |
| --- | --- | --- | ---: | --- |
| 0 | 12 | MuMu模拟器12 | 57,142,149,120 | 整盘SHA相同、启动/ADB/原MAC通过 |
| 1 | 15 | MuMu安卓设备-1 | 35,130,063,360 | 整盘SHA相同、启动/ADB/原MAC通过 |
| 2 | 15 | MuMu安卓设备0 | 52,439,285,760 | 整盘SHA相同、启动/ADB/原MAC/游戏IPC通过 |

三份原数据盘合计144,711,498,240B（134.773GiB）。完整SHA比对在**首次启动前**完成；随后正常开关机/应用启动会写入安卓数据，不将其与迁移复制损坏混淆。第三方应用分别可列出9/2/12个，WVD使用的原2保有原游戏包。首次就绪观察上界分别为129.5/37.8/30.0秒，包含管理器/轮询因素，不作精确启动性能基准。

原生安装自动生成的两个全新空实例已在导入前按创建时间、路径与约95MB数据盘认领后移除，未删除原用户实例。随后顺序导入原0/1/2并恢复名称。显式新建的本轮实例3实际VDI位于`F:/Applications/MuMuPlayer/vms/MuMuPlayer-15.0-3`，路径无reparse，旧C根未重建；验收后用原delete入口移除。三个原实例最终仍为0/1/2。

## 安装入口问题与处理

此前仅核签名、SHA与版本便把模块包当成独立安装向导，入口预检不足。用户确认UAC后没有后续界面，原生日志明确`from_orchestrator != 1, exit 1`；不是用户漏点界面。签名正确不等于全新安装入口已验证。

本机10月24日原6.8.8安装日志保留了真实调用链。本轮据此以同一事务`7e5f8c72-f62b-4216-8e8a-a3b0debd1ed3`完成main/两个engine准备、两个engine提交、main最终提交。六步exit0，原生prepared/committed状态与最终版本均核实，关闭auto_start/auto_run，TEMP/日志集中于F盘。没有执行第三方便携化代码、伪造配置或手工注册驱动；管理员确认由用户处理。今后卸载前必须核可执行的完整安装链，不能只验材料身份。

三个实例各只有一份原生`.mumudata`备份，全部通过完整7za成员CRC；迁移恢复后逐盘SHA与原件相同。CLI导出/导入均曾先返回-502而后台继续，未重复提交。还逐文件校验406份补充文件，并恢复16份共享素材、原MAC和必要用户偏好；旧.nemu、header/runtime及启动失败日志没有覆盖新运行树。原按键、手柄和脚本配置由原生卸载保留/安装恢复链承接，解包返回0。

导入前后三份customer/shell/vm配置相同；extra仅有原生创建时间、导入来源及原2关联ID变化。开关机后仅增加生命周期时间戳和主机窗口几何变化，CPU/内存/渲染用户设置未变；有旧XML的原0/2实际CPU、RAM硬件定义逐项相同。13份恢复配置及生成的运行XML均未引用旧C安装根。

## WVD和实际游戏验证

Git全局http/https代理已从失效17897修正为Clash7897，普通远端查询通过。本轮没有改其他代理/VPN节点。

正式WVD EXE仍为`15670137f3229039d088363af5580cf72665857b3a0a0af767f955c96811fa75`，data-root仍为`D:/programcode/python/wvd/next/.local/c11-flow-product/data`。经原入口临时启动并确认Idle/busy=false，通过profile CAS只改变EMU_PATH语义到`F:/Applications/MuMuPlayer/nx_device/15.0/shell/MuMuNxDevice.exe`；存储层同时更新values/default_values的路径及revision，原EMU_INDEX=2、ADB16448和其他设置保持。其余40份配置文件SHA不变。新revision为`950f77a8aaa726b37b74ece09f44670c4007aaabe7d2114526460d32fc34c5c2`。随后正式服务经原入口停止，没有构建、替换正式候选或提交业务任务。

设备验证使用全新独立的F盘data-root与17655，复制41份配置作为可丢弃输入，不写正式测试记录。直接使用同一已核EXE的原生入口启动隔离服务，未调用manage_service改写候选绑定；前后service-launch.json SHA相同。

三实例都实际开机、boot_completed=1且ADB shell响应，原MAC保留。原2按原Clash外控START_CLASH入口连接原VPN，tun0 UP；通过MuMu原生app launch打开原游戏，繁中启动提示页可见。WVD真实完成连接和**MUMU_IPC、900×1600、前台jp.co.drecom.wizardry.daphne**取图。没有进入悬赏或提交循环。

保留两项试验事实，不改写成全绿：首次冷连接返回一次DEVICE_CONNECT_FAILED，当时独立ADB就绪探针也在连接同地址，未保存首次原始ADB回执，具体时序原因未证实；设备就绪后同一原入口连接成功。最初未启动原VPN时游戏未保持前台，首次游戏取图元数据未就绪；补足原VPN前置条件并走原生应用入口后通过。没有据此推断唯一原因或做推测性生产补丁。相关回执保留在下述trash中的独立验证目录。

## 收尾、材料与当前边界

- 三个安卓实例及管理器关闭；正式17654、隔离17655和capture-host进程退出，三个VDI可独占只读打开。共享ADB有一个非本轮设备，未停止daemon或中断该设备；其小型日志目录保留。MuMu正常后台系统组件不冒称已卸载。
- 四个桌面/开始菜单入口已核F盘；其中旧“MuMu多开器12”保留名称/参数，改为当前F盘管理器。另只读核16个任务栏/隐式固定链接，未发现旧MuMu目标，没有动无关链接。
- 循环保持停止，原200目标仍为已核24/200、剩176，run6 UserStopped/2单元不计成功；没有新批次或成功轮次。监控PAUSED。
- 无commit/push、无线上CI、未改.vscode或生产源码。原生profile路径变更属于本轮实际迁移配置，不是测试写入。
- 本轮9项闲置执行/测试材料、737文件、118,238,811B已同盘重命名至`F:/trash/mumu-migration-temporary-20261031-7e5f8c72`，根FileID保持，未永久删除；同盘归档释放空间0。恢复映射为其中`RESTORE-MANIFEST.json`。
- 收尾另发现仓库根新生成`configs/customer_config.json`与`vm_config.json`共407B，初始Git状态不存在，创建时间紧随本轮两次导入，内容为MuMu手机/reset配置；未捕获具体写入进程，不据此虚构子进程。核无Git跟踪/其他文件后，逐文件SHA及根FileID保持，同盘移至`D:/trash/mumu-cli-cwd-20261031-7e5f8c72/configs`。后续CLI显式用F盘程序工作目录，实际info核对未重建仓库configs；正式data-root及旧config目录未动。两盘合计归档739文件、118,239,218B，均非空间释放。
- 唯一回滚材料集仍在`F:/Applications/MuMuPlayer-migration-20261030`：backup三份原始档、同版本签名安装材料、源配置/registry/快捷方式及必要回执。原生extra.importFilePath仍引用这三份备份路径，不在未核清消费语义时整体改名/删除；不再叠加完整快照。控制目录中的共享ADB日志亦未当作无租约垃圾移动。

完成摘要：`next/.local/mumu-f-migration-20261030/completed-migration.json`；详细总回执：F盘材料集`control/final-migration-receipt.json`。核心证据为`backup0/1/2-verified.json`、`source-vdi-sha256.json`、`restored-vdi-0/1/2-verified.json`、`F-install-verified.json`、`boot-0/1/2-verified.json`、`new-instance-default-F-verified.json`、`wvd-formal-path-updated.json`和`final-stop-verified.json`。完整设备链/首次连接失败回执现位于trash的`control/wvd-device-validation/`。

恢复使用原生导入入口，先保全当前数据与索引；不能覆盖当前VDI、回写旧C路径.nemu、直接导入旧.reg或重复运行本轮安装/导入脚本。官方[更改安装路径说明](https://www.mumuplayer.com/help/win/switch-emulator-installation-path.html)仅作为方法背景；本机实际同版本事务、导入和开机证据才是本轮验收依据。
