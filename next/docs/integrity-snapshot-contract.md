# 运行资源快照契约

本轮实现只进入独立离线核心。完整负例矩阵尚未验完，不能据此放行实机或生产。

## 生命周期与权威

1. 作者 Bundle 是来源。源清单列出全部成员及 SHA256，不能包含目录模板、通配路径、ADS、越界路径或重解析点。
2. `materialize_bundle` 用 `BundleLease` 锁住源文件和目录，从同一持有句柄读取、校验、复制到新的私有目录。没有硬链接，也不改写原文件。
3. 活动快照重新取得全文件只读句柄。文件共享模式仅 `FILE_SHARE_READ`，拒绝写入、删除和替换；目录句柄保护身份。文件内容从这些句柄计算 SHA256 后保存在本次 lease 中。
4. SDK 只接收活动快照路径。Pipeline 的 `template` 引用必须是已登记的具体文件；目前安全拒绝目录引用，尚未实现编译期目录展开。
5. 作者目录之后改变不影响正在运行的快照。重新加载仍必须校验源字节，不能用旧缓存接受不一致清单。同一作者规范路径与 revision 的不同清单被持久化 revision 索引拒绝。
6. Gateway 在 Tasker、Controller、Resource 全部退出后清空缓存并释放 lease。原生未返回或 STOP_TIMEOUT 时不提前解锁。不增加后台完整性线程。

实现：`native/storage/runtime_bundle.*`、`native/platform/windows/bundle_lease.*`、
`native/maafw/{gateway,preflight}.*`、`native/games/wvd/vision/asset_resolver.*`。

## 每次调用

- 每次新识别仍验证 lease/revision、参数及闭合目录成员集合。目录校验仍有遍历成本；“不重复全文件哈希”不等于 O(1)。
- direct Custom 调用在外层完成检查后，生成一次性、精确绑定参数、ROI、binding 与调用编号的内部凭证；真实 Custom 回调消费同一凭证，避免第二次整包校验。
- 原生 Pipeline 回调没有外层凭证，自行执行一次校验。作者参数和 override 中不允许注入 `_wvd_verified_invocation`。
- AssetResolver 从 lease 的已校验字节解码；不同 lease 的缓存不能混用。截图、代次、参数和 SDK 调用范围仍参与识别缓存身份。
- 模板请求、OCR 模型和 Pipeline 路径都不能绕过成员清单。运行期多出文件会拒绝下一次识别，不会把额外文件交给 SDK。

## 已测与未测

已测：真实 Gateway 快照初始化、文件写/删/替换拒绝、作者副本变更隔离、成员增加拒绝、
关闭后释放、旧清单拒绝、revision 重用拒绝、单次 direct→Custom 完整性边界。
最终结果与固定性能窗口见 [M3 修复报告](m3-fix-validation.md)。

仍需补齐：可写父目录竞态与完整重解析/路径矩阵、初始化各失败点、共享写入冲突、
延迟模型读取、跨 pack/revision 缓存组合、STOP_TIMEOUT 的专门资源锁断言，以及目录模板的编译期展开。
这些缺口不以已有测试数量代替；完整性和成本整体暂不放行。

## 私有持久化数据

- `<snapshot_parent>/<unique_id>/`：本次 SDK 活动资源及结束后的诊断副本，来源清单与 hash 是关联权威。
- `<snapshot_parent>/revisions/<identity_hash>.json`：作者规范路径与 revision 对应的清单索引；原子首次创建，同身份不能换内容。
- 未指定 snapshot_parent 时只允许 `.local` 中的隔离 Bundle，物化到其父目录的 `run-resources`；正式作者包必须显式指定私有目的地。
- 当前没有快照保留期或自动删除。磁盘保留成本是开放项；不能静默删源文件、用户配置或旧日志。
