# 动作耗时记录

## 目的与生效边界

记录每个动作在哪里耗时，支持后续优化；不增加截图，不改变点击间隔、识别条件、超时或恢复策略。当前已部署candidate32，新增记录已真实写出；此前candidate31只保留原日志，不能补造之前没有记录的数据。

## 数据位置与关联

- 每轮原生数据目录`runs/<instance>/<run>/action-timing.jsonl`。UTF-8，一行一个事件；由现有RunStore写入，不受工作台最近256条事件窗口影响。
- 每行带schema、instance_id、run_id、generation、UTC毫秒时间、type和payload。动作通过source_path、action_epoch及basis_frame关联；sequence为单Session内的输入尝试/片段序号，不是跨轮全局ID。
- 截图仍沿原最近帧及异常截图机制保存，不为计时额外截图。action_epoch与frame_id用于核对实际证据，不用15秒历史采样间隔推断取图速度。

## 记录口径

| 类型 | 含义 |
| --- | --- |
| input.attempt | 尝试/接受/拒绝/送达未知/异常，输入坐标、帧、代次及原因；结束行含门禁到提交返回的耗时分解。accepted只表示提交，不表示游戏已生效。 |
| input.result | 真正后置条件命中才记confirmed；重复菜单确认仍在原页记no_progress；Session结束仍待确认记unconfirmed。保留首次提交以来等待、最后一次提交以来等待、尝试次数，以及事件和读取故障暂停并集。 |
| timing.segment | 某节点连续执行段，含节点、来源、深度、轮询数、Session内开始偏移、墙钟及分类耗时。节点/事件切换才结段，不逐帧输出。segment_end只是分段，不等于成功；同节点重复进入可以有多个片段。 |

分类复用现有固定计数器：pixel_capture、pixel_convert、capture_metadata、input_validation、budget_queue、match、parallel_wait、recognition_other、json_events、explicit_wait、archive_submit、input_delivery；另有实际匹配/帧/缓存/ADB等计数。

单位为纳秒，用于展示时除以1000000为毫秒。wall_ns是经过时间；exclusive_ns是主线程互斥分类，worker_exclusive_ns为并行线程工作量，不可叠加到墙钟。unattributed_ns保留未归因时间，不把余额全部归给识别。节点片段、输入提交和结果等待存在包含关系，不能三者相加当总时间。result_wait包含正常游戏动画、网络、轮询和事件，不声称全是网络延迟。

## 开销与保留

- 同节点等待不逐帧写盘，只在输入边界及节点段结束写入并刷新，避免崩溃时丢掉大量已完成记录。没有额外日志线程或全局增长数组。
- 每轮文件上限64MiB，不删除旧日志；达到上限停止该轮追加。结果diagnostics.action_timing含rows、bytes、limit_bytes、dropped、failed、write_ns、complete，截断/写失败不可冒充日志完整。
- Session的performance另记timing_segments、timing_errors；写入失败不重放输入、不改变游戏结果。日志完整性和业务成功分别检查。
- 汇总慢动作时按source_path分组看次数、平均值、P95、最大值，并检查confirmed比例和重试；需要结合分类决定优化取图、识别还是游戏等待。未确认动作不能排除出慢动作统计。

## 验证边界

本轮受影响原生构建、候选打包及diff检查通过，不重跑旧大矩阵。candidate32 EXE SHA256为`7a59bf8820d8116851fc59dc33a2dd16f13f9a2fc21a1fb1898973617362b0f1`，构建日志`next/.local/logs/action-timing-native.log`，打包日志`next/.local/logs/action-timing-package.log`。

第4轮结束后首次切换曾被策略拒绝；用户随后明确授权强制关闭并部署。旧run6正常停止落盘、断开设备后强制结束PID36348；candidate32 PID39856接管原17654端口及原数据目录。

新实例`04BAA4AD-78C5-41B4-AF62-C4976BCE56B9`：run1从郊外启动未被Boot接受，主动停止；人工返回王城后，run2正式续跑96轮，请求`260941e6-6eef-4ddc-bf0d-3179f6244bd6`。累计目标仍为原完成4轮+96轮，不计两次中止尝试。

已核对run2真实JSONL含timing.segment、input.attempt accepted以及input.result confirmed。示例实际提交约327.08毫秒、后置条件确认约249.29毫秒，数字只代表该动作，不推断所有动作性能。日志生效已确认，完整新候选循环与长期日志增长仍按后续实际结果观察。

## 2026-10-02 实跑49轮耗时画像

范围为candidate63正式运行目录`data/runs/D22CE3AB-1E48-40A0-BF82-F5AD42F3FE4C`的run1–49：只解析既有result、动作计时和内存采样，没有跑测试或操作游戏。分析脚本及结构化结果保存在`next/.local/menu-restart-20261002/analyze_candidate63.py`、`candidate63-log-analysis.json`。

49轮均完整完成，平均232.666秒，范围219.279–244.841秒。各段Session平均78.187/97.172/56.114秒；Session总墙钟均值231.473秒，与整轮相差约1.2秒的外围处理。主线程互斥分类如下，未叠加worker工作量：

| 分类 | 平均秒/轮 |
| --- | ---: |
| 模板匹配 | 88.572 |
| 等待并行识别 | 46.343 |
| 显式等待/轮询休眠 | 47.909 |
| 截图元数据查询 | 23.419 |
| 输入上下文校验 | 12.661 |
| 纯像素截图 | 3.176 |
| 像素转换 | 0.213 |
| 其余识别逻辑 | 5.220 |

每轮平均306.45次实际截图、6504.31次实际模板匹配、259.14次ADB客户调用。6504是生产`matchTemplate`调用计数，不是测试/断言数量。平均约21次匹配/帧；IPC像素获取加转换约11ms/次，取图不是当前主瓶颈。显式等待包含必要游戏动画、导航与轮询，不能把47.9秒全部认定为可删除的浪费。

### 已确认的优化候选

1. **组合条件求值过量。** `native_recognizers.cpp`的all/any会求完全部子条件，纯条件启动两路分片；这与已按序首命中停止的boot_probes不同。匹配加并行等待占约135秒/轮。应先按当前流程/后置收敛必要条件，再考虑把资源/参数预检与运行条件求值分开，使纯布尔条件安全短路；不能吞资源Error、误用旧帧或移除输入反证。
2. **行动者识别读取无关方案角色。** `combat/turn.cpp`从全部STRATEGY构建portraits，`native_operations.cpp`在prepare循环识别全部头像，`CombatStrategy::select`最后才按当前方案筛选。当前配置7个方案共9种角色，法术+地裂只用6种；应按当前生效策略（含当前已切换的特殊战斗方案）筛选候选，保留同角色外观别名，不删其它方案。Actor_Entry每轮累计约9.66秒，不能说每一次角色选择都等9.66秒。
3. **跳轮确认后固定等10秒。** `bounty_cycle.cpp`在Leaped已确认郊外页面后仍delay_after(10000)，日志Task_Leaped每轮约10.27秒，其中9.94秒为等待。可改为下一页面可操作的新帧证据驱动，异常时沿现有等待/恢复；不是直接盲删网络等待。
4. **元数据与输入校验开销较高。** 约36秒/轮。capture每秒失效缓存时分别查dumpsys window和input；输入提交已合并为一次shell内前焦点/视口/后焦点查询。优先检查截图侧合并传输和同代次有效缓存，不能删掉焦点夹持、视口或包名门禁来换速度。

慢后置确认：跳轮17.443秒、回哈肯后楼层页12.056秒、自动导航确认11.693秒、住宿确认9.094秒。数字包含识别及实际页面响应，尚不能据此把全部延迟归因网络或图标阈值。run18入本Step1Found补点一次、两次提交后13.619秒确认成功；其它已记录输入结果没有补点、送达未知或失败。保留的事件中未发现读取恢复；此陈述不等于证明从未出现任何瞬态网络现象。

### 内存边界

private采样run1首183.44MiB/尾206.54MiB，run10尾390.47MiB，run30尾412.79MiB，run40尾430.51MiB，run49尾417.50MiB；全窗口最高采样487.86MiB。不是逐轮单调增长，未见此次49轮OOM/资源错误，但相较启动有明显增长，不能宣称已排除泄漏或完成归因。

run49识别资源缓存约3.3–4.3MiB，估算工作区峰值约249.4MiB、并发匹配峰值3；估算值不是实际常驻分配。日志仍存在900×1600全图模板搜索，须结合条件缩减/局部识别检查工作区压力。后续只利用自然运行采样追踪平台/工作区释放，不重新跑大矩阵或长内存测试。本次仅调查并记录，未改生产逻辑、部署或恢复循环。
