# 完整功能基线

固定生产基线：`6585f4075f5714ab522aa582993860c09af912c1`。M0 的后续提交只改变验证/文档，
当前 5 个生产 Python 文件与此基线内容一致。没有 import 旧模块或加载用户 config/mod。

## 覆盖结果

| 项目 | 数量 | 核对方式 |
| --- | ---: | --- |
| 顶层/嵌套函数 | 250 | 独立 AST 逐个源位置核对，含签名、装饰器、函数体 hash |
| 配置表字段 | 33 | 分类、旧拼写、类型、默认表达式逐项比较 |
| 基础任务 | 58 | 43 dungeon、15 quest，原 JSON 对象完整比较 |
| 任务 JSON 字段 | 1347 | 每个叶子/空容器的 JSON Pointer、原值独立展开比较 |
| GUI/CLI 入口 | 82 | 回调、绑定（含 bind_class）、无 command 的可操作控件及绑定变量 |
| 条件/分发分支 | 784 | 732 个 if + 52 个 case；含主消息循环与全部专项任务分发 |
| 类/枚举 | 24 | 基类、类级字段与默认表达式 |
| Lambda | 195 | 回调原表达式和位置 |
| 运行字段写入 | 337 | self/setting/runtimeContext/quest 属性写入单独分类 |
| 源码字典字段 | 221 | 构造、下标访问及 get/setdefault/pop，包括完整策略行字段 |
| 构建/更新/来源约定 | 6 | 基线文件 hash 与业务说明 |
| 资源/语言文件 | 440 | Git 路径与内容 SHA256 |

详表：[feature_inventory.json](feature_inventory.json)；语义补充：[behavior-contracts.md](behavior-contracts.md)。
每项均有原符号/任务 ID、原语义/表达式、源位置、新所有者、拟定入口、数据映射、验收族与状态。
**MAPPED_NOT_IMPLEMENTED 仅表示登记去向，不表示已迁移或可切换。** 拟定入口是逻辑责任，
不是已存在的 C++ 方法；资产/任务条目由其原 ID 指定查表参数。

静态调用记录共 4304 条：1346 条按限定作用域解析，2958 条保留外部/动态表达式。
没有用跨文件“只存在一个同名函数”猜测调用绑定；没有运行时追踪或游戏可达性证明。
所有 quest 类型任务 ID 都在真实 FARM_TARGET 模式分发中找到，其他源码分支也完整登记。
未知 mod 内容不在这份固定基线里，后续导入阶段须按用户数据再核对，不据此宣布完整生产迁移。

## 大小写差异

详表：[asset_case_report.json](asset_case_report.json)。420 条模板引用中，368 条精确匹配、
8 条大小写不符、44 条动态引用待 M3 用实际上下文核对。基线静态模板调用没有缺失项，
但不能据此推导动态名称或用户 mod 都可用。

| 原引用 | 原位置 | 基线实际文件 |
| --- | --- | --- |
| dungflag | script.py:1939 | dungFlag.png |
| returnText | script.py:2159 | ReturnText.png |
| returntoTown | script.py:2163 | returntotown.png |
| dungflag | script.py:3251 | dungFlag.png |
| mapflag | script.py:3335 | mapFlag.png |
| Steel | script.py:4269 | steel.png |
| Mark_auto | White-G-v2 / _TARGETINFOLIST / 0 / 0 | mark_auto.png |
| malice_B3F | malice-B3F / _EOT / 1 / 1 | malice_b3f.png |

Windows 可能容忍这些拼写，Linux 未必；M1 只报告，未修改引用或重编码图片。
任务字符串仅在可确认为已知模板名时进入此资源表；其他字符串仍完整保存在任务字段表，
不能把动作、描述或模式值误报成缺图片。

## 更新与验收方式

`tools/inventory/generate.py` 从固定 Git 对象重新生成，不手改 JSON；先修改职责规则或解析器，
再生成并运行 `tests/test_inventory.py`。工具各模块分别负责提取、归属、资源核对和输出。
截图和 API 测试使用独立 next 服务或临时目录，不写权威配置。未来每个业务切换应按验收族
逐条增加真正的生产链测试，再修改对应状态，不以“目录存在”代替实现完成。
