# M3 资源校验入口补验

## 已复现缺口

资源审计首轮 4 方法中 2 通过、2 失败（14.380 秒，`m3-integrity-entry-audit.log`，私有根 `m3-fixes-_svzgejo`）。这是新增入口覆盖，不重跑固定性能窗口或 M0。

- 既有九项 lease 负例仍通过。
- 同名模板在同 revision 不同目录、不同 revision 的包间交替调用，通过 Gateway SDK、Gateway Custom 和 OfflineRecognizer，两个场景各六组观察均不串包，输入为零。
- 作者模板/模型变化后才首次调用 SDK 识别，活动快照仍正确命中；OCR 三个模型的活动写入均被拒绝。这里只证明延后发起的调用，不推断 SDK 内部究竟何时预加载模型。
- 新增活动成员后，direct SDK、direct Custom、OfflineRecognizer 与 Pipeline Custom 都拒绝。但 Pipeline SDK 模板和 OCR 均继续执行了后续记录节点（`reached=1`、SDK 状态 3000、没有 failure）。这是实际漏检，不能用文件锁有效来掩盖。

## 修正设计

Gateway 提交根任务、Context 提交子任务前核对资源快照。原生 Context sink 在 `Node.Recognition.Starting` 读取本次实际节点定义，非 Custom、非 DirectHit 的识别自行核对闭合成员；Custom 仍消费已有精确凭据，DirectHit 不加载图像资源。Context 与 Tasker sink 分开注册，不能混用 opaque handle。

发现资源异常时标记 Gateway 不可继续、关闭输入门禁并报告原始错误；后续自定义动作不再执行。SDK 事件回调是通知而非可返回 false 的识别回调，所以不声称能取消已经开始的任意原生算法，也不改写 SDK 的原始识别详情。应用层拒绝继续/成功，资源锁和闭合引用清单保证异常新增项不成为 SDK 模板输入。

新增“任务已提交、首次截图时新增成员”的故障，防止只在 `post()` 增加一次检查就把漏洞算修好。该故障不改变截图内容，不根据截图次数推进剧情。

SDK direct 的目录成员检查统一在原生开始事件执行，不在 direct 预检重复遍历；参数预检仍校验具体文件与闭合引用。direct→Custom 继续消费一次性凭据。新增 SDK/Offline 单次目录检查断言，首个原生入口错误必须保留到观察结果。以上仍待构建实测，不是成本放行，`PERFORMANCE_UNRESOLVED` 与 `RESOURCE_UNRESOLVED` 保留。

## 当前状态

首次审计产物已保留。入口修正与中途变化负例已写入，尚待构建和实际复验；不宣布修复通过。当前运行中的陷阱测试使用修正前已封存的 M4 EXE，不混用证据。

固定 SDK 的 Context/GetNodeData 和事件 handle 以本机已锁定头文件为准；行为另对照 [官方 Context 实现](https://github.com/MaaXYZ/MaaFramework/blob/2bcfa85c66a2eac6ca3e5937f175495275ee0643/source/MaaFramework/Task/Context.cpp)。M3 查询所有权、其余资源矩阵、真实设备与生产切换边界不变。
