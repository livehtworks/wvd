# 当前架构与业务分层

## 当前有效调用链

```text
automationd main
  -> HttpServer (单 Asio I/O 上下文，管理活动连接)
      -> route (请求校验、只读 API、静态资源)
          -> contracts/version (接口事实)

Vue MigrationPage
  -> useInventory (只读响应镜像、筛选与分页)
      -> api/client (同源 GET，有限超时)
  -> InventoryTable / ItemDetail (无请求、无任务副作用)

tools/inventory/generate
  -> ast_scan (提取) / mapping (职责映射) / assets (资源引用核对)
  -> 固定 Git 基线 -> JSON 报告 -> Web 构建时只读镜像
```

原生 `app` 不放路由或业务，`api` 不认识 WVD 策略，组件不发设备动作。
盘点工具与服务没有运行时依赖关系；服务不访问 Git，也不 import Python。
HTTP 连接最多 64 个，每次读/写上限 5 秒，头/体上限 8/4 KiB，静态资源上限 32 MiB。
断开连接即移出活动集合，不保留无界历史。关闭先取消 accept/连接，再处理完取消回调并退出。
这里验证的是 **HTTP 服务正常停止**，不能推导 Maa 原生等待可中断。

## M2 运行核心

```text
独立调用者 -> RunCoordinator（请求去重、运行快照、设备租约、监督与恢复决策）
  -> ExecutionSession（有限会话工作线程，每代独立状态）
      -> MaaGateway（真实 Pipeline、子任务详情、根任务证据、SDK 对象释放）
          -> GuardedAction
              -> 新截图 -> 场景/目标识别 -> ActionIntent
              -> Maa 内置动作或 Controller 队列
              -> GuardedController -> InputGate -> DeviceBackend
              -> 新截图 -> 后置确认
  -> EventJournal / RunStore（有界事件、原子终态与证据）

OfflineRecognizer -> 同一 MaaGateway / preflight / RecognitionDetail 三态转换
```

`wvd_core` 是单独的原生库，构建选项默认关闭；M1 `automationd` 不链接该库。
所有运行请求在连接前拒绝真实后端；测试提供离线设备，Maa Pipeline/识别/回调本身不是 Mock。
只启用固定 CPU OCR 模型；没有 WVD 业务或第二套节点调度器。迁移清单仍未宣称业务已迁移。

### 所有权与停止

RunCoordinator 的监督线程不调用 SDK 阻塞等待；会话工作线程持有全部原生对象。
用户停止先关 InputGate，记录 StopRequested。正常静止后才提交 UserStopped。
原生调用未返回或按住的触点/按键未释放时，超时进入 Failed/STOP_TIMEOUT，quiescent=false，
继续保留会话和 Windows 设备租约，拒绝新运行。没有 detach、强杀或后台替代控制者。
正常回收必须等回调离开，再依次销毁 Tasker、Controller、Resource、userdata。
因此外部测试 watchdog 未触发不等于底层等待可任意取消，资源增长也不因此归因完成。

恢复只能在前一 Session 真正结束后由协调器决定，使用新 generation 和新 SDK 对象；
恢复决策回调是纯决策，不执行设备操作。有限次数与会话时间预算由冻结运行定义给定。
重复 request_id 返回已有运行；不同配置重用同 ID 拒绝。每实例最多记住 256 个请求，
达到上限显式拒绝新请求，不淘汰旧记录后误执行迟到重试。

### 输入与终态

门禁检查实际帧的设备、游戏、包版本、代次、viewport、动作 epoch、时效、场景、应用、权限与坐标。
所有回调先记 attempted，再记 accepted/rejected/backend_called；停止后只允许已按住输入的释放。
外层截图统一为识别尺寸，SDK 在这一层比例为 1；InputGate 到底层原始尺寸只映射一次。
不同宽高比拒绝，不自动旋转或裁切。SDK Scroll 的附带 TouchMove 也被观察和拒绝，
其后真正的 Scroll 仍须通过同一个单次许可，不放行额外点击或移动。

子任务检查真实 TaskDetail 状态，不以 ID 有效代替成功。根终点还核对 task_id、generation、
嵌套层级及指定终点节点。引擎 Succeeded 但业务终点未到达仍失败。
业务终态和终态事件同存 result.json，原子提交成功后才对外发布 Completed。
存储失败不发布终态成功，不覆盖旧文件；数据职责见 [数据权威](data-authority.md)。

## 后续层边界

| 层 | 唯一职责 | 后续阶段 |
| --- | --- | --- |
| native/runtime | 已实现运行、有限 Session、业务终态；不含游戏知识 | M2 |
| native/maafw | 已实现统一 Gateway、三态识别、回调和停止映射 | M2 |
| native/devices | 已实现门禁与后端契约；真实 MuMu/ADB 适配未接 | M2/M3 |
| native/platform | Windows / Linux 平台机制 | 分平台验收 |
| native/storage | 已实现运行快照、原子结果、有界事件；配置迁移另做 | M2/M4 |
| native/games/wvd | 视觉、战斗、路线、补给、恢复和任务业务 | M3/M4 |
| packs/wvd | 可验证的游戏包内容 | M3/M4 |
| web/src/features | 流程/视觉编辑草稿及调试呈现 | M5 |
| schemas | 应用配置与包契约 | 对应实现阶段 |

其余未实现目录只记录边界，未创建虚假的执行类或未接通的 API。
游戏层详细划分见 `native/games/wvd/README.md`。未来依赖方向为：
应用组装运行协调器；运行协调器通过约定能力调用 Session；Session 经 Maa/设备门禁执行；
WVD 业务提供决策与领域结果，不让通用 runtime 判断技能、地图或宝箱。

## 数据权威与隔离

- 原 `config.json`、mod、任务入口和更新/打包链仍归 Python 生产程序，本轮完全未切换。
- M1 没有数据库或用户配置写入。基线权威是报告里的固定 Git commit，不是前端状态。
- `feature_inventory.json` 是生成的迁移索引，条目状态全为 MAPPED_NOT_IMPLEMENTED。
- 预览 PNG 来自原 Git 资源，不是实机识别证据；不将用户截图或本机目录加入清单。
- 前端不拥有 C++ 指针、不发任意 shell、不决定游戏循环，不重试任何启动任务命令。

## 维护约束

新增业务前先补完整链路的输入、终点、副作用、恢复与验收映射，再按领域实施。
不得将旧 Factory 翻成巨型类、把整个 Farm 作为 Custom，或重新实现第二套 Pipeline 调度。
注释解释所有权、并发、失败边界和旧语义保留理由，不用逐行叙述掩盖过长函数。
代码格式由 `.clang-format`、Prettier 和 Python Black 统一；格式化仅限 `next/`。
