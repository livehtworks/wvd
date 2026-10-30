# 巨人50轮日志、性能与内存核查

## 范围与证据

日志核查阶段仅只读分析，不改产品代码、配置、运行数据，不操作游戏、不部署、不启动新循环。用户随后单独授权报告与既有修复源码commit/push；提交包含本报告、死亡过渡/启动衔接修复和其定向检查，不加入新的业务修复。当前candidate119/PID39784仍为Completed/quiescent，repeat completed/34/34/inactive。已部署候选的历史构建身份不回写，不能将带未提交补丁构建伪装成新commit重新构建。

50轮由两个不同进程/候选组成，不能拼成同一进程的内存斜率：

| 组 | 成功运行 | 另保留的中止运行 |
| --- | --- | --- |
| candidate117 | 57479A08-CE93-4390-A8F2-66EE8BB5AB65/run1–16 | run17 Failed/party.death_interrupted |
| candidate119 | 6F203752-F7C8-4956-8020-80566D1AD79B/run2–35 | run1 Interrupted/revival.outcome_unconfirmed |

既有analyze_run_timing.py分别校验17和35份记录：结果身份、二进制身份、事件序号/行数、动作耗时行数及内存诊断均通过；统计成功轮时再排除两个中止。完整事件为依据，不以UI尾窗替代。Get-ChildItem对已写文件可能显示0字节，本次按实际读取内容、结果声明和行数校验，不报告日志丢失。

独立分析产物在next/.local/log-review50-20261006/，不写入正式data：timing117/、timing119/的run-timing-analysis.json，review-summary.json、combat-review.json及对应只读脚本。原事件、结果和诊断文件保持不变。

随报告提交的[数值摘要](giant50-log-memory-20261006.json)保留两组逐轮耗时/私有内存峰值/收尾阶段、所有者与句柄计数、二进制身份，以及本机完整分析文件SHA256。它不包含截图、用户配置、完整日志或进程名单，不替代本机原始证据；内存单位MiB，斜率单位KiB/轮，各阶段不互相替换或相加。

## 已发生的异常

1. 两次中止不计成功：原死亡提示误判Pause，以及再起后黑屏/重连导致旧输入未确认。入口和死亡过渡等待已修；复活跨重连保护仍保留未闭合事实，不能因后来50轮完成就撤销。
2. 成功续跑run34仍发生设备故障。combat-open-detail/open等待中出现ADB_TIMEOUT（capture:dumpsys window）、ADB_TRANSPORT_FAILED（capture:exec-out screencap -p），随后DEVICE_INSTANCE_RESTART_REQUIRED/STARTING。重连Boot_Poll又触发连续异常62.250秒升级重启。最终恢复并Completed，耗时606.790秒；没有证据把底层故障归给工具内存或单一应用。
3. 面具开技能8次no_progress：run6/11/12/16两次/21两次/31，9–10次尝试，等待20.790–21.422秒后执行Skill0UnavailableDefend，并记录defend_fallback_confirmed。当时面具已识别，分数0.924–0.974，不能称为头像漏识别。缺少这些旧轮当时的技能面板原图，尚不能确认是SP不足、等级不可用、菜单时机还是按钮未生效；不能拿当前画面或头像分数替代原因证据。
4. run34的同一开技能等待达48.652秒，但包含70次ADB调用和传输恢复，不能当成普通模板匹配耗时。标题补点4–5次、约13–17秒后confirmed，属于慢启动得到确认，不应删掉容错。
5. 续跑34轮，7414条input.attempt对应3707次attempted/3707次accepted，没有rejected/error/unresolved；有861次prepare选中、23次portrait_unrecognized后再观察。没有Auto输入。34次入本Fallback2Present是正常备用入口，不计作战斗保底；战斗保底只有上述8次。记录35次200G住宿提交，含旧现场收尾，不把提交记录当成游戏必然扣款证明或额外完整循环。

## 内存：确定的来源与未归因增长

| 指标 | candidate117成功16轮 | candidate119成功34轮 |
| --- | ---: | ---: |
| 私有内存采样峰值范围 | 720.9–858.6 MiB | 740.7–882.0 MiB |
| worker_joined首/末 | 70.05 / 93.79 MiB | 73.23 / 118.35 MiB |
| 首/末5轮收尾中位数 | 78.08 / 91.73 MiB | 80.23 / 117.96 MiB |
| 本窗口线性斜率 | 1485.44 KiB/轮 | 1184.46 KiB/轮 |
| 收尾句柄范围 | 361–365 | 362–365 |

这是真实同阶段增长：candidate119收尾首末净增45.11 MiB，不能说已解决。斜率仅描述本窗口，不外推长期固定泄漏；不同候选/进程的斜率也不能当修复收益比较。最终批次配置释放时124.67 MiB/312句柄，之后本次只读进程采样103.95 MiB/311句柄；这些不同边界不能替换worker_joined序列。

现在已经有具体所有者证据：

- 冻结资源租约原文件缓冲135.53 MiB：模型93.82 MiB、JSON27.73 MiB、图片13.91 MiB等。BundleLease::Held::read确实把文件内容读入vector并保持到租约释放；owners=4是共享引用，不能乘四。这不是全部资源都被OpenCV解码。
- 每会话解码素材缓存仅2.66–3.63 MiB，结果缓存示例约13.4 KiB，普通帧缓冲4.12 MiB。这些数目不支持“模板图片缓存独占800 MiB”的判断。
- candidate119成功34轮创建/销毁102组OCR（3个会话/轮），初始化进程私有提交变化中位145.55 MiB，销毁变化中位-277.23 MiB。对应变化是进程配对观察，不是OCR独占分配账本；推理期间还有外部运行库与其他并行分配。英文OCR示例未初始化，繁中OCR初始化，不能说每轮同时加载两种OCR。
- 工作区估算峰值示例249.39 MiB、并发匹配峰值4；它是MatchBudget的在途估算，不是已定位249 MiB真实分配，不能拿来与进程峰值直接相加或求残差。程序参数容器约12.43 MiB同样是容量估算。
- 成功34轮每轮worker_joined的Service/OCR/Session live都归零；最后进程总创建/销毁均103，含中止run1。没有OpenCV异常记录，句柄没有逐轮增加。对象账目归零不证明CRT堆、ONNXRuntime或线程运行库没有残留。

结论：高峰有资源租约和OCR运行时的具体证据；45 MiB收尾抬升仍为RESOURCE_UNRESOLVED，现有日志不能区分真实未释放分配、分配器缓存和碎片，不能擅自归为OpenCV泄漏或“只是缓存”。

## 整机压力

candidate119有544条系统压力采样，记录窗口提交占用90.2%–99.25%，中位95.0%；可用物理内存最低2.01 GiB。最高压力发生run5/generation2，前8名可读私有提交包括MuMuVMMHeadless 4862 MiB、MuMuNxDevice 3430 MiB、两个ChatGPT进程3008/1319 MiB、automationd799 MiB等。

这是整机提交容量接近上限，不是物理内存百分比；不可读进程、内核及共享内存未被前8名完整覆盖，不能简单加总归因，更不能断言run34故障由run5的峰值造成。本次未结束任何用户应用、修改分页文件或重启模拟器。

## 性能与优化顺序

candidate117成功轮平均382.00秒/中位370.69秒；candidate119成功轮平均445.95秒/中位411.29秒（约7分26秒/6分51秒），最慢707.14秒含启动旧现场。排除启动run2和设备恢复run34后平均432.76秒。这是总轮耗时，不是纯战斗秒数，不构成两个版本的控制变量性能比较。

candidate119成功轮主执行线程平均：模板匹配154.93秒、并行等待65.05秒、元数据48.19秒、识别其他48.69秒、输入验证31.40秒、显式等待79.90秒；像素抓取9.13秒、转换0.77秒、输入发送0.43秒。不能叠加节点、worker和主线程墙钟，不能把时间差都算到截图解码。

最高热点Actor_Entry平均95.42秒/轮，34轮共150044次实际匹配、3182次抓帧；全轮约20510次实际匹配、650次抓帧和430次ADB调用。Native turn的Entry重新检查Ended、AutoOff、Speed、Automatic、Prepare等；组合识别还检查全部子项。冷启动就绪、输入授权和错误传播必须保留，但正常战斗动画可优先等待已声明战斗进展，再在可行动菜单分类，避免对每个无输入动画帧重复走全部菜单候选。不得缩窄技能/敌人ROI、降阈值或把未识别变Auto来换速度。

建议按以下顺序推进，不在本次核查中执行：

1. 内存归因换到分配栈，而不是再增加私有内存标量日志。绑定automationd进程与匹配PDB，使用WPR/WPA的Heap与VirtualAlloc路径，在少量正常实跑的会话前、OCR推理后、worker_joined比较仍存活分配的调用栈/数量/大小；区分JSON/图像/模型原文件、OpenCV临时内存、ONNXRuntime及线程运行库。输出有限ETL与差分，不再用50/100轮替代归因。机器有wpr.exe及automationd.pdb；UMDH/GFlags未在PATH定位到。本次未开启系统跟踪或修改注册表。
2. 先做有独立所有者证据的资源优化设计：审查135.53 MiB原文件缓冲中哪些运行消费必须使用bytes，哪些只需被锁定文件；保留资源身份/哈希/防篡改契约，禁止直接清content或放开写共享。OCR的3次/轮加载应结合 allocation stack再评估固定所有者下复用，不能直接延长缓存生命掩盖收尾增长。
3. 针对8次面具no_progress补永久异常证据：在原no_progress→防御分支保存当时技能面板和结果图、角色/技能/等级、资源错误识别及真实回执。沿既有有界RunStore失败证据写入，不增加新截图线程或无限保留；普通滚动240张已覆盖不了几小时前的非致命异常。
4. 减少Actor_Entry与已知转场的重复识别。现有DeviceSession元数据1秒刷新一次，每次dumpsys window+dumpsys input；先用实际耗时分解找重复查询/输入验证能复用的同身份事实，不删除前台与旋转检查，也不直接放宽帧龄/连接/epoch门禁。
5. 系统压力以证据告警并记录到具体恢复时间窗，不自动关闭用户应用。run34设备故障与复活跨重连保护分别处理，避免把所有问题统一归给内存或网络。

官方方法依据：[WPR资源分析](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-for-resource-based-analysis)区分Heap和VirtualAlloc；[UMDH说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/using-umdh-to-find-a-user-mode-memory-leak)要求符号与不同时刻的堆分配栈差分，不能用一次进程快照判泄漏。未来采集应限定目标进程、时间/大小并保留结束/还原操作，不开启长期全系统堆跟踪。
