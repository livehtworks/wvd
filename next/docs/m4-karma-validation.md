# M4 善恶选择与新版配置写回

## 分层和固定语义

`games/wvd/karma` 仅计算选择和新值；`recovery/karma_prompt` 编译有限 Maa 图；`WvdRunState` 只保存选择、幂等编号和确认事实。`state_factory` 是装配点，通过 `KarmaCommitPort` 注入 `storage/karma_writer`，文件路径、revision 校验和 CAS 不放进业务决策。

保持固定旧源 `IdentifyState` 的字符串规则：数值零选择 ambush 并置 `+2`；原字符串以 `-` 开始则选择 ambush 并加二（正数不补 `+`）；其它选择 ignore 并减一、前置 `+`。使用既定 Boost 的多精度整数，不能把 Python 整数缩为 int64。ASCII 空白与合法数字分隔下划线沿用原解析；无法解释的输入明确报 `KARMA_VALUE_INVALID`，不覆盖导入值。非 ASCII 数字的 Python 等价性尚未实现，不计完整字段迁移通过。

## 事务边界

先观测提示并准备选择，缺少显式新版 profile 绑定时零输入失败。每次点击仍独立验证场景与目标；仅新帧证明两个选择图标都消失且进入已知场景时提交确认。旧代码的“Press 未生效也更新善恶值”不保留。提示未消失或后置未知只报告 `karma.choice_outcome_unconfirmed`，不复点、不重启。停止/输入拒绝不写回。

确认后先保存 Run 内操作编号、原值/新值和帧/代次，再执行新版 profile CAS。保存冲突、锁冲突、原子替换失败均结束为 `PROFILE_SAVE_FAILED`，保留已执行事实与具体存储错误，禁止自动重发。正常保存更新最新回执及 revision；同一操作回放不重复扣减。`legacy_document`、passthrough 和旧配置文件不变；`values` 才是新版有效字段权威。schema1 增加可选 `last_business_update`，旧文件缺字段不冒充已有回执。

## 当前验证

七目标构建通过（`m4-karma-confirmation-build.log`），状态组 18 方法通过，耗时 5.480 秒（`m4-karma-state.log`）。包括 19 个旧公式对照输入、真实新版文件确认前无写入、确认去重/跨代次去重及第二次选择消费最新值。流程组正在验证，不能以状态测试代替输入链。

流程待验证项为三种实际存储故障、未绑定/非法值零输入、停止/拒绝，以及未知/不消失/Retry 覆盖结果不能算成功。所有文件只在本次 fixture 新建私有目录，SDK 与输入门禁不替换。

首轮流程 6 方法中 5 通过、1 失败（74.487 秒，`m4-karma-workflow.log`，私有根 `m4-workflow-kyqohu26`）。失败是新增断言错误地在 Run 通用原因中寻找子流程原因；实际最后 Session 已保存 `karma.choice_outcome_unconfirmed`，仅一次输入、无生命周期操作、profile 未变。已按现有契约同时断言 Run 的 `RECOVERY_REQUIRED` 和 Session 的具体原因；不改运行逻辑，原失败保留，完整重验中。

本批被测 EXE SHA256：`a4eda3371c986eb5886bc7e345e334697da48ceb07429ca3fcb97b5774d7bb77`。Retry 即使保留底层地下城图标，也不得作为确认成功证据。

本阶段不代表完整 TaskID、其它配置回写或跨进程业务恢复已完成，不改变 M3/M0 未决边界。
