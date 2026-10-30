# WVD 游戏包

本目录是当前原生产品的清单管理包输入。作者源、生成包、候选包与运行发布分别承担职责；当前规则以 [持久数据职责](../../../docs/data-authority.md) 为准，维护任务见 [维护事项](../../../docs/maintenance.md)。

## 当前基线与职责

以下数字固定于提交 `179cfd851877cba2f2df401d86a905ff9ae088f6`，依据 [2026-10-26 收束盘点](../../../docs/reviews/engineering/consolidation-inventory-20261026.md) 及该提交 Git 树、JSON 复核，不代表后续清理或索引修复已经完成。

| 对象 | 当前内容与用途 |
| --- | --- |
| `manifest.json` | 593 个成员：591 张 PNG 与两份作者 JSON；记录包路径、字节身份、来源及历史引用信息 |
| `image/` | 591 张 PNG，包内模板字节；总清单大小 16,221,432 字节 |
| `parameters/semantic-assets.json` | 作者语义资源的生成副本；当前 148 项语义资源直接引用 176 个不同模板名 |
| `parameters/public-flows.json` | 作者公共流程的生成副本；不取代正式 data-root 中的已保存用户流程 |
| 根 `resources/images/` 与 `next/resources/images/` | 已核来源分别有 437 与 158 个文件；来源图、包副本职责不同，不按重复 SHA 删除 |

两份 JSON 的人工编辑源为 `next/resources/authoring/`。当前 591 张包内图按其实际根目录或 Next 来源查找，源与包 Git blob 字节身份一致；两份作者 JSON 与包内副本也一致。此项远端复核与本地盘点的 SHA/大小检查分开记录。

显式生成入口为 `next/tools/package_functional.py` 的 `sync_authoring_resources()`；`next/tools/build.py` 在构建前调用该事务。`stage()` 校验既有作者输入并生成候选，不隐式回写作者源或包副本。候选的只读 `pack/` 与按请求冻结的 `published/<request_id>/` 各有身份和引用，不能直接用本目录替换正在使用的运行包，也不从活动作者源热读配方。

## 未完成的索引整理

- **24 条 `source` 路径已在C03修正。** 从错误的根 `resources/images/…`改为实际 `next/resources/images/…`，来源大小/SHA与包成员一致；沿原`files + aliases`算法得到revision `6e756fe42f4f15541ed28cd338bbf23fcf31754ab560c7a7d528eaf1db65cea2`。成员路径、数量、图像字节及运行语义保持。元数据属于新的产品输入，不将旧构建回执当作这一版已构建或部署。
- **295 张图的 `source_refs` 为空或缺失。** 其中 274 条为空数组、21 条缺字段；共 38 张仍被当前语义模板直接引用（17 条空数组、21 条缺字段）。该历史静态索引不能用作删除清单。
- **4 张 Next 头像原图未入包。** `next/resources/images/monster-portraits/` 中的 `cyclops.png`、`dark_enemy_candidate.png`、`red_hair_serpent.png`、`scorpion.png` 仍待核对用户头像、嵌入配置与来源用途，不据未入 manifest 判废。

当前候选判废状态与历史清单见 [废弃素材与待确认清单](../../docs/deprecated-assets.md)。修正 `source` 属于产品输入变更；manifest 的来源恢复与新候选验收仍须完成，不能把本次说明更新写成已修复。原 `source_commit` 表示历史来源基线，不能批量改成最新提交替代逐项来源证明。

## 历史 M3 / M4 说明（原阶段记录）

下文保留最初复制与迁移阶段的原始范围。其中 437 张、引用计数、“本轮”和 `prepare_m3.py` 均属于历史阶段，不是当前资源生成入口或全量验收结论。

M3 已从固定基线 `6585f4075f5714ab522aa582993860c09af912c1` 复制全部 437 张 PNG，
保留路径大小写和源字节。`manifest.json` 记录来源、hash、8 处显式引用映射及 44 处动态引用上下文。
它是作者目录，不直接作为运行 Bundle.root；装配时只复制 image/model/pipeline 内容到新的独立快照，
manifest 和运行日志位于内容树之外，避免 hash 自引用。
`web/public/reference-assets` 是从固定基线复制的忽略目录，仅用于检查实际模板，
不是正式截图样本，也不是完成迁移的游戏包。
本轮没有完整任务 Pipeline 或正式配置/mod 导入。测试只在隔离目录验证基线优先、mod 后备。
`prepare_m3.py` 可复核并生成；已有副本字节不符时拒绝覆盖，不通过重编码修正资源。
