# 恢复等待期间的哈肯页面交接

## 根因与修法

正式实例43E2B469-365A-4D61-8A47-4B3204F4CE0E第15轮，宝箱继续后frame338还带迷宫HUD，打开恢复输入81接受后进入哈肯。frame381已是哈肯楼层菜单，执行器却仍等待Task_FirstDungeon_Heal_Open0。事件3417在62690ms触发主动游戏重启；不能把这一超时当成游戏卡死的证明。

补给分类只有战斗、宝箱、再起等中断，漏掉哈肯。上层已有HarkenCompleted/HarkenArrived，但子调用不退出就无法重新分派。本次复用harken_floor_menu的原素材、ROI和阈值，经已有encounter出口返回原Dispatch。不新增处理器、输入或等待，不放宽Unknown送达保护，不伪报治疗、不清除恢复需求。上层仍结合原任务点账目确认返程点。

正常角色面板、迷宫及遭遇仍优先；只有这些不命中才判断哈肯。素材依赖沿原supply_scene_probes收集，未修改正式保存流程或建立第二份定义。

## 验证与交付

新增tests/fixtures/healing-harken原图与来源，扩展现有recorded_input_contract；只隔离设备传输，Service、NativeInputGate、FlowExecutor、补给/巨人导航工厂及业务状态均为生产代码。

- 旧代码回放已失败：10.675秒仍有1条pending、task_step=1、只发送原打开输入。证据在next/.local/hk-261019/baseline/。
- 新版要求：打开后转哈肯能返回原导航、已在哈肯时0输入；前序目标未完成不得跳过；不伪报治疗；按原结果结清普通输入。
- 新版原图三组通过：切页后1836ms完成原导航，1次原打开输入、pending为空、task_step=2；已在菜单时1618ms、0输入；未完成前序目标时保持task_step=0、0输入、不误完成。三组均保持healing_required=true、healing_submissions=0。
- recognition模块18入口complete=true（含先前LV3、恢复关闭、宝箱结算等回归），资源构建工具6项通过。使用全新可丢弃data-root，真实模型按锁定哈希装配；这不是游戏18轮。
- 由原build.py辅助函数、package_functional及manage_service完成原生构建、冻结打包、独立候选校验与正式部署。测试/产物输入一致：13792c29ca4649691ed86b7b66364062e9262349849d51524e773880ff5babcb/1311；EXE6108abc4ce9dd4abb6698398476bb459d42d7ff02571571ce64c1404b34b4024。
- 新服务87BA907F-E0A1-4093-B382-EACDB76F33D2/PID1668/start_filetime134368221936688707；17654端口和正式data-root保持，27份权威文件哈希不变。
- 已经原Application提交剩余85次，请求giant-85-harken-handoff-20261019-7959df634026457ab4139c4956c95b64。启动时旧现场为公会菜单，Boot_Poll未接走，事件69在61604ms触发一次原有游戏恢复；不是本次哈肯路径。免责声明事件115/frame47确认后，已越过标题、回到业务链，跳轮选择事件501接受/505在frame293确认，514接受跳轮。最新run1实际Running/repeat active、0新成功、累计15/100，有新帧和真实输入进展；并非只收到accepted。不宣称全程无重启、100轮完成或现场同条件哈肯交接已复现；启动菜单覆盖缺口独立保留。

## 进度与回滚

用户授权后停止原批次，保存Completed回执run1–15计15成功，run16为UserStopped，不计成功。其普通列表返回Sent原账保存在stopped.json，不伪造确认；原manage_service已正常关闭旧服务。目标仍100，后续只续85。

27份profile/workflows哈希及修改前源码保存在next/.local/hk-261019/。旧完整候选next/dist/wvd-next-native保留，新候选使用独立next/.local/hk-candidate-261019。人工回滚时先按当前匹配请求停止并确认静止，再通过原manage_service停止服务，以旧候选及原端口17654/data-root部署，不双服务运行、不自动回退、不倒写配置。

原始故障证据next/.local/rr-261019/；本轮回归、部署和续跑账目next/.local/hk-261019/。未授权commit/push，不新增定时检查。
