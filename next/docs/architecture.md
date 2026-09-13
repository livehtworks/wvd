# M1 架构与业务分层

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

## 已建立但未实现的层

| 层 | 唯一职责 | 后续阶段 |
| --- | --- | --- |
| native/runtime | 用户级运行、有限 Session、业务终态 | M2 |
| native/maafw | 固定 C ABI 与资源句柄/回调映射 | M2 |
| native/devices | 身份、连接、截图控制器与输入门禁 | M2/M3 |
| native/platform | Windows / Linux 平台机制 | 分平台验收 |
| native/storage | 配置版本、原子保存、有界事件 | M2/M4 |
| native/games/wvd | 视觉、战斗、路线、补给、恢复和任务业务 | M3/M4 |
| packs/wvd | 可验证的游戏包内容 | M3/M4 |
| web/src/features | 流程/视觉编辑草稿及调试呈现 | M5 |
| schemas | 应用配置与包契约 | 对应实现阶段 |

这些目录只记录工作包要求的边界，未创建虚假的执行类或未接通的 API。
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
