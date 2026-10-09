# 逐项整改索引

基线4c466e14fa514fa177380e902afe373c8f20c3b2。源码改动、限定断言通过、整包验收、部署、实机和归因分别登记；没有将43项批量关闭。

完整字段、原审计来源和当前实现文件见[ISSUES.json](ISSUES.json)，验收边界见[主报告](../architecture-audit-remediation-20261010.md)。`source_repaired_acceptance_partial`表示已有修复与部分真实生产代码断言，但尚有回执所列的必需覆盖缺口；不是已完成生产验收。`source_repaired_targeted_verified`也仅证明列出的限定离线合同。

| 问题 | 优先级 | 回执 | 原问题 | 当前状态 |
| --- | --- | --- | --- | --- |
+| EX01 | P0 | [WP01](work-packages/WP01-result.json) | 输入已发送后分配失败，未决回执消失 | source_repaired_targeted_verified |
| EX02 | P0 | [WP02](work-packages/WP02-result.json) | 历史重启布尔可被用来放行后来产生的未决输入 | source_repaired_targeted_verified |
| EX03 | P1 | [WP03](work-packages/WP03-result.json) | scrcpy资源在命令已生效但返回失败时可能失去清理归属 | source_repaired_acceptance_partial |
| EX04 | P1 | [WP02](work-packages/WP02-result.json) | 生命周期读取不共享完整绝对期限，超时/取消后仍可返回就绪 | source_repaired_targeted_verified |
| EX05 | P1 | [WP08](work-packages/WP08-result.json) | 有效事件规则在热路径反复复制、排序与序列化 | source_repaired_acceptance_partial |
| EX06 | P1 | [WP08](work-packages/WP08-result.json) | 每次tick构造完整进度，elapsed变化使去重失效 | source_repaired_acceptance_partial |
| EX07 | P2 | [WP05](work-packages/WP05-result.json) | 中立BusinessConfirm的condition不是执行器独立确认条件 | source_repaired_targeted_verified |
| EX08 | P2 | [WP05](work-packages/WP05-result.json) | 一般重查只认guard且重置计时，IR未强制完整输入前提 | source_repaired_targeted_verified |
| AP01 | P1 | [WP12](work-packages/WP12-result.json) | 内置流程同步没有UI承诺的备份，两文件提交可失配 | source_repaired_targeted_verified |
| AP02 | P1 | [WP11](work-packages/WP11-result.json) | 诊断图片地址和前端key不含运行身份，可能串轮 | source_repaired_acceptance_partial |
| AP03 | P1 | [WP11](work-packages/WP11-result.json) | current响应拼接多个独立快照，换轮可混合运行身份 | source_repaired_acceptance_partial |
| AP04 | P1 | [WP11](work-packages/WP11-result.json) | 状态读取与诊断I/O可能拖住停止准入或停止HTTP回执 | source_repaired_acceptance_partial |
| AP05 | P2 | [WP11](work-packages/WP11-result.json) | current轮询仍读取和传输整个有界事件环 | source_repaired_acceptance_partial |
| AP06 | P1 | [WP03](work-packages/WP03-result.json) | Application线程的收尾发布可抛出到jthread边界外 | source_repaired_acceptance_partial |
| AP07 | P2 | [WP03](work-packages/WP03-result.json) | 启动已交给协调器，watcher注册前仍有取消空窗 | source_repaired_acceptance_partial |
| AP08 | P2 | [WP12](work-packages/WP12-result.json) | 幂等查询晚于可变版本检查，256条历史又成为准入上限 | source_repaired_targeted_verified |
| AP09 | P2 | [WP13](work-packages/WP13-result.json) | 每轮与每文件配额没有组成发布/整批磁盘预算 | source_repaired_acceptance_partial |
| AP10 | P2 | [WP13](work-packages/WP13-result.json) | 辅助性能日志与关键输入/故障证据共享串行I/O和预算影响面 | source_repaired_acceptance_partial |
| AP11 | P2 | [WP15](work-packages/WP15-result.json) | CI和统一验收入口未覆盖当前原生产品的新增契约测试 | source_repaired_acceptance_partial |
| AP12 | P2 | [WP15](work-packages/WP15-result.json) | 关键协议分散在JSON、字符串、编译展开和手工文档中 | source_repaired_acceptance_partial |
| AP13 | P3 | [WP15](work-packages/WP15-result.json) | 能力元数据与当前文档入口保留旧阶段常量 | source_repaired_acceptance_partial |
| MP01 | P1 | [WP06](work-packages/WP06-result.json) | 连续批次采集的端点与导出器资格条件不兼容 | source_repaired_targeted_verified |
| MP02 | P1 | [WP07](work-packages/WP07-result.json) | 分析器不能提供当前承诺的free位置/VA，unknown栈还按地址膨胀 | tooling_repaired_live_allocation_attribution_unresolved |
| MP03 | P1 | [WP07](work-packages/WP07-result.json) | 采集父进程stdout/stderr仍无界ReadToEndAsync | source_repaired_acceptance_partial |
| MP04 | P1 | [WP03](work-packages/WP03-result.json) | 低内存清理路径还会分配，局部句柄未即时RAII | source_repaired_acceptance_partial |
| MP05 | P1 | [WP09](work-packages/WP09-result.json) | OCR每个文字框复制整图，并构造未消费的绘图和字符串 | source_repaired_acceptance_partial |
| MP06 | P2 | [WP09](work-packages/WP09-result.json) | OCR CTC末时间步漏掉最后类别，能误选成另一字符 | source_repaired_acceptance_partial |
| MP07 | P1 | [WP14](work-packages/WP14-result.json) | 依赖归档锁定没有约束实际被构建消费的解压文件 | source_repaired_acceptance_partial |
| MP08 | P2 | [WP09](work-packages/WP09-result.json) | 每unit/recovery新建OCR所有者，常规每轮三次初始化仍在 | source_repaired_acceptance_partial |
| MP09 | P2 | [WP06](work-packages/WP06-result.json) | 采集总期限未覆盖最后完整性检查/哈希，耗时报告失真 | source_repaired_targeted_verified |
| MP10 | P2 | [WP16](work-packages/WP16-result.json) | 每帧SDK尺寸查询与缓冲重建存在差异，渲染崩溃仍缺dump | unarmed_plan_delivered_external_crash_root_unresolved |
| MP11 | P3 | [WP07](work-packages/WP07-result.json) | 其它诊断脚本仍有整文件读取、逐行无字节界限或局部期限 | source_repaired_acceptance_partial |
| MP12 | P3 | [WP14](work-packages/WP14-result.json) | 打包生成阶段会先改资源源文件，多文件输出未成事务 | source_repaired_acceptance_partial |
| MP13 | P1 | [WP10](work-packages/WP10-result.json) | 匹配并发预算不是全局内存准入，OCR/解码绕过压力护栏 | source_repaired_targeted_verified |
| R01 | P1 | [WP04](work-packages/WP04-result.json) | 界面承诺的空配置继承变为Auto，未知组也自动化 | source_repaired_targeted_verified |
| R02 | P1 | [WP04](work-packages/WP04-result.json) | 先过滤剩余动作再识别角色，会选择次高分的错误角色 | source_repaired_targeted_verified |
| R03 | P1 | [WP05](work-packages/WP05-result.json) | 恢复输入未生效，返回迷宫也能确认healing_completed | contract_repaired_healing_effect_material_unresolved |
| R04 | P1 | [WP08](work-packages/WP08-result.json) | 补给图重复展开宽泛场景/反证/后置，缺阶段识别计划 | source_repaired_acceptance_partial |
| R05 | P1 | [WP08](work-packages/WP08-result.json) | 递归识别复制条件树、aliases、business summary和嵌套证据 | source_repaired_acceptance_partial |
| R06 | P2 | [WP14](work-packages/WP14-result.json) | 获准builtin的资源依赖闭包遗漏，完整基础包掩盖漏项 | source_repaired_acceptance_partial |
| R07 | P1 | [WP05](work-packages/WP05-result.json) | action_eligible=false没有统一禁止固定点/back/swipe输入 | source_repaired_targeted_verified |
| R08 | P2 | [WP04](work-packages/WP04-result.json) | Karma解析与分支使用不同字符串规范，空白负值能写坏后续值 | source_repaired_targeted_verified |
| R09 | P3 | [WP15](work-packages/WP15-result.json) | 编入产品的Fordraig/Cave工厂没有公开任务入口 | source_repaired_acceptance_partial |
