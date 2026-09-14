# M4 配置与保存边界补验

## 验收对象

本阶段使用独立临时目录，不读取或写入运行目录 config.json。旧覆盖语义只读核对固定 `6585f407` 的 `gui.py::LoadConfig`：先由 GENERAL 选择任务专用或 DEFAULT 区段，再以选中区段覆盖 GENERAL；选中区段本身也可能覆盖控制字段。

新增配置断言覆盖全部 33 字段的六种区段组合、不存在/空任务目标、空策略容器、未知中文嵌套值和嵌套重复键。修正测试驱动 `source or {}` 对 falsy 错误根值的归一化，确保空数组、空字符串、false、零真正送到正式解析器，而不是被测试代码改成合法空对象。

保存断言调用正式 ProfileStore：实际 Windows 独占锁使 CAS 返回 PROFILE_BUSY；允许读取但拒绝删除共享的文件句柄，使原子替换返回 STORAGE_COMMIT_FAILED。两者都逐字节保留原文件。两个同步起跑的原生写入者竞争同一 revision，必须恰好一个成功，另一个报告 Busy 或 Conflict，最终文件必须完整等于成功者；再提交陈旧稿不得覆盖成功者。

故障注入只存在于原生测试，正式存储代码没有故障开关。并发断言不是用顺序两次 CAS 冒充竞争，也不通过修改系统磁盘或正式文件权限制造失败。

## 当前结果

`m4-effect-profile-image-build.log` 构建通过。数据组 `m4-profile-boundaries-data.log`：11 方法通过，1.758 秒，私有证据 `m4-data-lnsubv1q`。状态及存储组 `m4-profile-boundaries-state.log`：12 方法通过，3.632 秒，私有证据 `m4-state-tests-g5591s7s`。

实际返回 PROFILE_BUSY、STORAGE_COMMIT_FAILED 并保留旧文件；并发写者恰好一个成功，陈旧稿返回 PROFILE_CONFLICT 且最终数据不变。本组不连接离线设备也不执行输入的存储场景，与通过真实 Maa 会话的状态场景分开记录。原 11 项状态方法仍保留通过，新增 1 项实际存储测试。

## 剩余

图片 mod 的实际发布接线和基线优先选择已另补源码，待同批构建后的独立资源测试验证；不能以任务 JSON 冲突测试代替图片来源验收。确认副作用后的 profile 写回及失败不重放属于 M4.8，尚未由本组证明。
