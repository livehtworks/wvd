# WVD 游戏包（M3 / M4）

M3 已从固定基线 `6585f4075f5714ab522aa582993860c09af912c1` 复制全部 437 张 PNG，
保留路径大小写和源字节。`manifest.json` 记录来源、hash、8 处显式引用映射及 44 处动态引用上下文。
它是作者目录，不直接作为运行 Bundle.root；装配时只复制 image/model/pipeline 内容到新的独立快照，
manifest 和运行日志位于内容树之外，避免 hash 自引用。
`web/public/reference-assets` 是从固定基线复制的忽略目录，仅用于检查实际模板，
不是正式截图样本，也不是完成迁移的游戏包。
本轮没有完整任务 Pipeline 或正式配置/mod 导入。测试只在隔离目录验证基线优先、mod 后备。
`prepare_m3.py` 可复核并生成；已有副本字节不符时拒绝覆盖，不通过重编码修正资源。
