# 战斗方案重命名入口

方案标题旁新增铅笔图标与“重命名”按钮。点击后选中名称输入，确认或Enter应用更名，取消或Escape保留原配置。空名称和重名明确提示，确认前不改引用；仍通过原CAS保存配置。

`strategy-frequency.spec.ts`更新为新入口，独立原生服务18758使用`next/.local/strategy-rename-1004/data`副本。桌面/窄屏取消、空名称、重名、确认及服务端保存重载通过，已有复制排序检查保留。截图已人工核对，正式配置未用于测试写入。
证据：`next/.local/strategy-rename-test.log`及`strategy-rename-web.log`。

旧closure中受影响的入口断言也同步更新。附加锁定检查已走到保存中重命名禁用及错误后草稿保留，但旧夹具缺少当前API产生额外NETWORK_ERROR，导致通用alert断言冲突，整项不记通过。失败日志保留在`strategy-rename-lock-final.log`。此前npm吞掉grep及端口不匹配的启动错误分别保留于`strategy-rename-lock.log`、`strategy-rename-lock-fixed.log`；执行注意项已记录。

candidate73已受管部署到17654，旧PID3136退出，新PID35348，实例`D33D44A3-890B-41B7-B45E-782DCF29948D`，Idle/run_id=0。页面返回`index-BcpyFydu.js`和`index-NPPfsNLz.css`；正式profile部署前后SHA256均为`1A5A5C4709644A3166F5C38F603C8BD44F4A49E8D54AC199E935EF94EF4338CB`。构建、打包、部署日志分别为`strategy-rename-native.log`、`strategy-rename-package.log`、`strategy-rename-deploy.log`。

临时18754静态服务与18758隔离原生服务均已退出。未操作游戏、未启动循环、未执行commit/push。部署后仅补记本文及滚动事实；回滚可受管Deploy切回candidate72并保持原data目录，本轮无配置格式迁移。
