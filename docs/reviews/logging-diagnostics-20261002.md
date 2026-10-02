# 日志与内存诊断交付说明

## 使用方式

工作台“设备与高级”内打开“日志与诊断”，设置级别、分类和细采样间隔，保存配置后在下一轮生效。当前轮使用`run.json`中的冻结设置。默认级别为`info`；`trace`最详细，`error`最精简，`off`关闭全部可选明细。

| 类别 | 开关与级别 | 每轮文件 |
| --- | --- | --- |
| 动作耗时 | “动作耗时明细”开启且不高于`info` | `action-timing.jsonl`中的`timing.segment` |
| 输入审计 | 始终记录 | `action-timing.jsonl`中的`input.attempt`、`input.result`和运行事件 |
| 内存边界 | “内存诊断”开启且不高于`info` | `diagnostics.jsonl`中的`before_session`、`session_owners_alive`、`session_owners_released`、`worker_finishing` |
| 识别汇总 | “识别统计”开启且不高于`info` | `diagnostics.jsonl`中的`session_statistics` |
| 细采样 | “内存诊断”开启且级别为`debug/trace` | `recognition-memory.log`，默认每1000毫秒，允许1000至60000毫秒 |
| 单帧截图元数据 | “动作耗时明细”开启且级别为`trace` | `diagnostics.jsonl`中的`capture.frame` |
| 识别资源故障 | 始终记录 | `recognition-memory.log`中的`resource_failure` |

现有事件流加入`level/category`字段，但不按日志开关删除状态、恢复或业务事件。异常截图仍由原RunStore准入与配额控制。`diagnostics.jsonl`按轮限16 MiB，`action-timing.jsonl`沿用64 MiB限额；写盘失败、超限分别写在终态诊断汇总中。用户主动关闭的类别标记为`collected=false`，不算诊断写盘失败。计时分析器在动作明细关闭时明确报`TIMING_COLLECTION_DISABLED`，不把缺项算零。

## 内存解释边界

边界记录包含主进程PID、进程创建时间、私有字节、工作集、句柄数；`run.json`包含实际EXE SHA256、任务/资源修订及本轮日志设置。`session_owners_alive`附识别缓存、匹配并发与已知raw帧缓冲估计；同一raw缓冲在Session最后帧与恢复帧之间只计一次。`session_owners_released`在局部所有者析构后采样，并用弱引用确认Session及识别器释放。`worker_finishing`在终态提交前采样，它不是线程真正join之后的样本。

这些统计不覆盖OpenCV和OCR全部内部堆、`std::any`游戏时序资产、截图宿主进程或模拟器；不能用进程私有字节差直接推定某一组件泄漏。需要在自然多轮、同EXE身份下比较“已释放”边界的回落趋势，再决定是否做独立的堆归因。

## 本轮验证

- `automationd` Release增量构建通过；Vue类型检查通过。
- 隔离profile API测试覆盖默认值、CAS保存、重读和非法间隔拒绝；未连接设备。
- 隔离RunStore检查覆盖关闭可选日志时保留输入审计、类别与级别；单轮内存边界检查确认顺序及两个所有者已释放；未操作游戏。
- 独立数据目录的工作台在桌面/手机视口完成正式API保存与刷新回读，截图已由Playwright生成；临时服务经正式shutdown退出。
- 上述限定检查不构成实机内存归因或长期稳定性验收。

## 后续授权部署与实跑

- 用户追加要求commit/push、部署并执行50次蝎女任务。源码提交`7af267c`已推送`fork/agent/local-stability-notes`；资源同步和CMake配置后冻结源码，完成Vue与Release构建，产物整理为`next/.local/c11-flow-product/candidate64`。
- 经正式`manage_service.ps1 -Action Deploy`预检和生命周期入口部署，沿用原data目录与17654端口；未覆盖旧`config.json`、mod或历史运行日志。
- EXE SHA256：`f459472e640927787985f20c02bdd9a06ea415d7d5063a0d569f6f5bb728d3bf`；服务实例`9F20ECA2-DEFE-431B-9F47-E9E55F5C2146`。运行日志实例与服务实例职责不同，不以UUID相同作为判断条件。
- 启动请求`ccac0078-1c55-4992-a237-dda9a9f21a60`，`Scorpionesses`、`zh-Hant`、`repeat=true`、`repeat_count=50`；日志批次`next/.local/c11-flow-product/data/runs/1ADE8F5C-EBC0-4B3B-8987-82327F5F1ECB`。
- 通过正式profile CAS仅调整日志至Debug、内存/动作耗时/识别统计开启、1000毫秒细采样；原自动Clash/VPN开启、战斗方案及任务配置保持。首轮生命周期记录确认VPN就绪及游戏前台启动，繁中下载确认正常通过，随后进入王城、公会悬赏页和跳轮。
- `run.json`已冻结EXE身份与日志策略；`diagnostics.jsonl`内存边界、`recognition-memory.log`细采样和`action-timing.jsonl`均已实际落盘。当前为运行中采集，不宣称50轮已经完成或内存问题已解决；异常仍由既有恢复、截图和终态机制记录，不手工抹掉失败续计成功。
