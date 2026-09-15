# 分阶段图发布与续段

统一构建通过，发布与续段两方法通过；不是完整任务通过报告。

`14e9eb3`阶段产物的证据根为`m4-workflow-iglq4ii1`。正常例Completed、两个业务单元、
两代次各一次真实受控输入，入口分别Stage0_Entry/Stage1_Entry，资源revision相同；
持久化结果一致、mismatch=false、quiescent=true。缺第二图Economy.png的负例在连接前
拒绝，连接/输入均为零。两份execution.json与EXE身份已在重建前核对。
首轮`m4-workflow-gimiyrtn`因夹具缺descriptor在启动前exit1，保留失败记录；
补明确描述表路径后取得上述证据，没有放宽发布校验。后续公共代码变更仍需回归。

## 约束与实现

`RunCoordinator`要求同一Run所有正常业务段共享资源revision。Fordraig十段和
分离洞窟六段不能分别发布不同资源包后绕过该限制，也不能将所有路线拼成一个
超过30分钟预算的大Session。

`tasks/workflow_session.cpp`新增`publish_workflow_stages`，沿用单图的源文件锁、
资源解析、mod来源与哈希封存、注册表校验、只向不存在目录发布的规则：

- 一次解析和锁定全部阶段所需资源，任何后续阶段缺图都在连接前拒绝。
- 每图增加`StageN_`命名空间，仅重映射节点边、子任务入口/重置集合和确认操作ID；
  图片名及视觉条件不作字符串替换。各图独立验证，4096节点/30分钟上限不变。
- 各图保存为独立Pipeline文件，同一资源revision覆盖所有图、时限、策略和注册表身份。
- 每个Session仍保存自己的入口、根终点、业务检查点和冻结的对话策略；
  一个阶段完成不等于整个任务完成，最终结果由同一个RunCoordinator管理。
- 恢复策略使用上一Session定义，保留当前阶段命名空间，不跳回首段重新领取任务。

单图入口仍调用同一发布实现，没有第二个资源加载器、运行器或活动期补图逻辑。
CLI只生成计划和检查资源，不连接设备；真实离线运行入口位于既有native测试夹具。

## 验收断言

`test_stage_publication.py`通过正式Maa离线因果设备验证两图共用revision、
不同入口的正常续段、各自根终点和逐段输入；缺第二图资源必须零连接/零输入拒绝。
这只是发布与续段契约验证，不能替代Fordraig、分离洞窟的完整任务和恢复矩阵。

单图原有发布、mod、子任务边界、停止和M2运行回归仍须执行。
构建日志为`m4-extensions-and-stages-build.log`，不将旧EXE的通过结果归到这个变更上。
