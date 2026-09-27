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
