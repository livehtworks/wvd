# 项目当前事实

## 当前产品

- 当前产品在 `next/`，C++20/CMake + Vue + OpenCV + ONNX Runtime；Maa和旧Python链不参与原生构建/运行，历史源码、config、mod、dist不删除。
- 唯一执行链：Application → NativeRunCoordinator → NativeExecutionSession → FlowExecutor。Application拥有循环；没有新增并行执行器、兼容别名或自动回退。
- 配置/流程/回执数据权威见 [数据职责](data-authority.md)，执行注意项见 [操作约束](execution-notes.md)，核心语义见 [核心合同](../next/docs/core-contracts.md)。

## 当前整改

- 用户指定的 `wvd-full-architecture-audit-4c466e1.zip`源码整改/限定离线交付已整理：基线4c466e14fa514fa177380e902afe373c8f20c3b2，54份文件哈希/大小一致。16包不是全部验收关闭，43项未批量closed。
- 本轮只做源码、独立离线/Windows定向验证及文档；未改正式profile/用户流程，未部署、连接游戏、启动循环或现场内存采集。`.vscode/`用户修改未动。
- [逐包整改报告](reviews/architecture-audit-remediation-20261010.md)、[43项索引](reviews/architecture-audit-20261010/ISSUE_INDEX.md)及16份JSON回执保留历史验收维度。上一轮整改已提交/推送ee024eb；原回执null是当时状态，不重写历史。
- [后续修复](reviews/architecture-audit-followup-20261010.md)：流程读缓冲OOM句柄泄漏已旧失败/新通过，未跟踪源码漏验与依赖模块映射已修复，跨实例同编号PNG/延迟旧读在实际RunStore通过。六模块后续回执complete=true，产品输入hash60b68ff66c1b5f7733280e6f81cefec56c8f6b9d59dbd148b03906fc64c047e7，随后续源码一并提交；未部署或启动游戏。
- 最终final-contracts-14六模块入口complete=true：39个原生命令、11项工具/构建，另HEAD检查通过；源/10个EXE/4个分析器产物身份复核，产品输入hash为56a9ac97f30875dfd18931f573d2fe57d0616f60f29924111796a1e3a36d242e。这里的50项不是游戏轮数，也不覆盖全部工作包验收断言。
- WP03/07/08/09/11/13/14/15有逐SDK故障、真实ETL/复杂图成本、并发图片端到端、批次边界、全部最小builtin/旧反例等必需覆盖缺口，具体由回执限定。缺疗效素材时补给保持Unconfirmed；自然崩溃计划UNARMED。未提升为正式运行结论。
- 当前测试入口是 `next/tools/run_contracts.py`，必须指定模块/改动基线和全新隔离输出。个人fork的原生PR工作流已添加，远端CI尚未执行。

## 正式现场基线

- 本轮开始时最新正式部署candidate138，原17654服务；最新UserStopped、busy=false、quiescent=true、repeat inactive。此后没有操作正式服务或设备，不能据旧状态声称它此刻仍存活。
- 最后批次8/41完成、第9轮停止；跨版本/跨恢复累计67/100，不是连续100轮通过。停止前Heal段46.352秒、1199次实际模板比对，取图转换0.334秒、识别调度34.906秒。
- 实机已验证的历史蝎女/巨人流程仍有证据；59项公开目录没有被禁用。Fordraig/Cave工厂是未公开休眠实现，不算新增可运行任务。
- 原操作许可：实例2、繁中、已有VPN配置，金币可按任务使用；禁止绿色/紫色宝石购买、抽卡、卖装备。当前包不授予这些现场操作的新增许可。

## 开放边界

- `RESOURCE_UNRESOLVED`：新聚合器/端点/屏障的离线验证不等于真实负载分配栈归因。后续短采集须单独授权，不再靠增加轮数或PrivateUsage标量关闭问题。
- libRenderer.dll/0xc0000409自然崩溃缺dump/故障栈；历史nvoglv64.dll签名、程序主动重启分别保留，不能混为原因。取证计划保持未启用，厂商SDK契约未证实前不改尺寸查询/缓冲复用。
- 治疗页面返回不证明疗效；正式疗效素材未登记时保留healing_required，返回未确认，不允许generic healing_completed绕过。
- 历史面具技能面板图缺失，头像命中不能倒推出技能可点；多人死亡页按用户要求人工停止；部分普通迷宫随机事件繁中素材仍待自然采集，不放行实际依赖缺失的流程。

## 历史索引

- [整改前完整状态归档](reviews/project-status-before-architecture-audit-20261010.md)
- [暂停问题交接](reviews/runtime-issues-handoff-20261009.md)
- [MuMu自然崩溃核查](reviews/mumu-renderer-crash-20261009.md)
- [架构审计本轮交付](reviews/architecture-audit-remediation-20261010.md)
- [架构审计后续修复](reviews/architecture-audit-followup-20261010.md)
