# 可组合流程工作包本地收口

## 结论

基线 `dcc11c18aff724ad9557bbbab13b064ad5734c24` 上的可组合流程整改已接入现有 Windows 产品。
公共步骤、流程块和任务共用原作者文档仓库、编译链、Maa Pipeline 与 RunCoordinator；没有第二套
执行器。当前交付可用于编辑和保存组合流程，但本轮没有真实设备操作，不能据此宣布公会领取、
跳轮或完整任务已经通过。

## 实现范围

- 作者模型：`call`、`slot`、`route`、公共接口、标量参数绑定、引用闭包和准确契约错误。
- 语义资源：统一语义 ID、语言变体、观察/位置角色和发布包哈希，不以英文素材替代缺失繁中素材。
- 后端：同锁冻结定义闭包、被引用删除保护、首次种子导入、编译来源路径和运行快照身份。
- 前端：公共定义分类、调用参数、插槽扩展、条件编辑、提取公共块、展开定义、返回原调用者及嵌套运行定位。
- 交付：12 份公共定义和语义目录进入 `next/dist/wvd-next/pack/parameters`。

架构与状态分别见 [architecture.md](architecture.md) 和
[composable-workflow-status.md](composable-workflow-status.md)。

## 定向验证

| 验收 | 结果 | 证明范围 |
| --- | --- | --- |
| `automationd` Release 构建 | PASS | 修改后的 Windows 服务和应用装配可编译 |
| `test_public_flow` 显式构建/运行 | PASS，11 项 | 参数隔离、真实 Pipeline 编译、定义闭包、嵌套来源、循环和资源缺项拒绝 |
| Vue 正式构建 | PASS | TypeScript、Vue SFC 和生产资源可构建 |
| `flowModel` 定向测试 | PASS，15 项 | 默认值、类型、公共块提取、失败边界和循环预算保持 |
| 隔离 HTTP 编辑链 | PASS | 创建、两次参数调用、插槽扩展、保存重开、展开、返回调用者、删除引用拒绝 |
| 生产页面检查 | PASS | 调用参数和插槽面板可见，返回调用者恢复原节点，控制台无错误 |
| 打包目录启动检查 | PASS | HTTP 200、12 份公共定义可读取，未连接设备 |

首次前端模型测试命令使用了相对模块路径，测试脚本按工作包目录解析后找不到仓库产物；该轮没有
执行用例，不计结果。改用编译产物绝对路径后 15 项全部通过。

交付 `automationd.exe` SHA256：
`67C622B521416B1ED311431A0C0584F661AF25C13A1A567674A67C67F20EEEBE`。

## 保留边界

- 所有现有游戏任务仍为 `LEGACY_NATIVE`；`AUTHOR_DEFINED=0`，`REAL_SCOPE_VERIFIED=0`。
- `guild.commissions.page`、`guild.bounties.page`、`guild.bounty.first_item`、
  `guild.bounty.receipt` 缺可靠繁中 recipe，相关公共组合保持阻断。
- 没有运行全任务矩阵、内存测试、MuMu、ADB、VPN、游戏或不可逆领取动作；设备操作计数为 0。
- 旧 `src/`、`config.json`、`mod`、日志和用户 `%LOCALAPPDATA%\WvdNext` 数据未修改。
- 验收使用独立 `next/.local` 数据根；结束时 17659/17660 端口均释放，`automationd.exe` 为 0 个。
