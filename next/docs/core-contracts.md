# 当前核心合同

解释入口与真实调用链对应，不另建产品执行器，不依赖历史报告猜许可。

| 合同 | 唯一解释入口 | 不变式 |
| --- | --- | --- |
| 输入事实 | PendingInput/PendingDelivery、FlowExecutor | 底层调用前登记Prepared/Attempted；Sent/DeliveryUnknown先落已有对象，后续分配失败不抹未决输入；确认结果才结清 |
| 页面与业务 | Observation/PageAuthority/page_authority、NativeInputGate | scene和target均需Hit且eligible；固定点/back/swipe不绕过；BusinessConfirm独立检查condition，页面返回不是效果证明 |
| 有效策略 | effective_strategy_name、配置规范/角色选择 | 空任务配置继承唯一全局，未知名拒绝；先选当前最佳角色，再看剩余动作，不用次高分替代；频次按实际释放回执 |
| 恢复证明 | InstanceExitProof/instance_exit_covers、生命周期deadline | 绑定设备/实例/创建身份/旧新连接/退出时间/事务；捕获≤提交≤退出，旧证明不清后来输入；取消/超时先于ready |
| 资源闭包 | CompiledWorkflow/builtin_probes/ModelContract/manifest | 实际语言、隐式模板/OCR模型均须冻结且哈希一致；缺失为Error并拒绝发布，不当NoHit，不靠完整基础包掩盖遗漏 |

## 运行与读取

- Application → Coordinator → Session → FlowExecutor唯一链；Application拥有批次，旧Python/Maa不参与原生构建/运行。
- current一次ReadView；事件按cursor读取并绑定instance/run/gen，缺区间明确resync。图片URL含instance/run/gen/id并核不可变索引/哈希，不使用“当前图片”别名。
- 停止不等command mutex、准备磁盘I/O或诊断图片写入完成；stop_epoch阻止停后新准备。收尾仍等真实worker退出，不能用停止请求成功代替静止。
- 一个根run拥有OCR模型池，unit/recovery借用；取消终止当前运行，不能清活跃引擎后复用。生命周期配对/归零不证明所有Windows提交增长已归因。

## 识别与预算

- Service内有界叶子身份登记，同帧/epoch共享；并行重复叶子只有一生产者。组合和业务状态不跨帧缓存，语言/ROI/阈值/TTL不放宽；any/all仍传播所需叶子Error。
- 有效事件清单按program/step/active scope缓存；语义进度与elapsed心跳分开，不每tick构造整份进度。
- 并发workspace与OS压力准入分开；系统提交、进程虚拟余量、可查询Job进程提交上限独立。未知Job当前总提交不用峰值代替。模板/解码/预处理/OCR共用票据，压力是Error，不重放输入或清pending。
- 采集屏障默认关闭；worker_joined/heap-maintenance同阶段端点绑定request/instance/run/gen/config/sequence/创建身份。到期/stop取消，joined不改名为batch_payloads_released；容量/期限/覆盖不足明确拒绝分析。
- 输入审计与性能采样分级限额；普通采样丢弃报告sampling_complete=false，不假造完整、不因它终止业务续轮。关键输入遗漏仍阻断；关键失败图有独立额度与永久run索引。

## 验证与公开范围

`tools/run_contracts.py`核对实际CMake目标，记录源hash、EXE hash、隔离根、限定通过项；未指定模块/基线或隔离输出拒绝。12组既有Flow回归保留。Fork PR CI执行受影响模块；CI、部署、游戏实测和内存归因分别需要回执。

Capabilities描述源码支持和证据要求，不写固定实机/部署结论。公开任务目录 `packs/wvd/parameters/legacy-quests.json` 的59项由run_builder分派，未删任务。Fordraig/Cave工厂未被目录公开，保持休眠维护范围，不声称用户入口或实机通过。缺素材/dump/厂商SDK契约明确未确认。
