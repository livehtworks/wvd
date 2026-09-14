# M3 资源封存负例补验

日期：2026-09-15。只补工作包的完整性矩阵，不重跑性能窗口或 metadata 已阻断组。

## 实测结果

固定 Windows 工具链构建后，2 方法全部通过（2.975 秒）：原活动快照测试和新增 9 场景。

| 场景 | 结果 |
| --- | --- |
| 正常完整清单 | 建立并释放 lease |
| 文件预先被写者持有 | INTEGRITY_SHARING_CONFLICT |
| 错误哈希 | RESOURCE_HASH_MISMATCH |
| 错误 manifest 哈希格式 | BUNDLE_MANIFEST_INVALID |
| 大小写冲突 | BUNDLE_CASE_COLLISION |
| 最后成员缺失 | INTEGRITY_SHARING_CONFLICT |
| 持锁目录改名 | 操作被拒绝，原目录仍有效 |
| 持锁期间新增目录 | BUNDLE_DIRECTORY_CHANGED |
| 资源根下存在 junction | BUNDLE_LINK_REJECTED |

每个场景都再逐个申请原始成员的独占写句柄，证明成功退出或初始化中途失败后没有残留文件锁；未写入文件，target 哈希保持不变。目录/junction 仅在每个测试新建私有目录中创建，未访问用户设备和资源目录。

原快照测试仍覆盖活动文件写/删/替换被拒、作者副本变化隔离、新增文件被拒、释放后修改成功、旧 hash 及复用 revision 拒绝。没有删除或减弱其断言。

证据根 `m3-fixes-mwbmh37d`，日志 `m4-integrity-negative-matrix.log`。每例 `execution.json` 保存原生 EXE 哈希，结果核对实际加载的固定 Maa/OpenCV DLL 路径及哈希。纯 lease 负例不冒充 SDK 识别；原快照测试使用真实 Maa Gateway。

## 保留边界

此组不证明完整 SDK 延迟加载、全部跨包组合或长期资源稳定，PERFORMANCE_UNRESOLVED / RESOURCE_UNRESOLVED 不变。MetadataQuery/CLEANUP_PENDING 两轮后阻断仍保留，不以本次资源锁测试替代其所有权证明。
