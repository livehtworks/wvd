# M4 善恶选择与新版配置写回

## 分层和固定语义

`games/wvd/karma` 仅计算选择和新值；`recovery/karma_prompt` 编译有限 Maa 图；`WvdRunState` 只保存选择、幂等编号和确认事实。`state_factory` 是装配点，通过 `KarmaCommitPort` 注入 `storage/karma_writer`，文件路径、revision 校验和 CAS 不放进业务决策。

保持固定旧源 `IdentifyState` 的字符串规则：数值零选择 ambush 并置 `+2`；原字符串以 `-` 开始则选择 ambush 并加二（正数不补 `+`）；其它选择 ignore 并减一、前置 `+`。使用既定 Boost 的多精度整数，不能把 Python 整数缩为 int64。ASCII 空白与合法数字分隔下划线沿用原解析；无法解释的输入明确报 `KARMA_VALUE_INVALID`，不覆盖导入值。非 ASCII 数字的 Python 等价性尚未实现，不计完整字段迁移通过。

## 事务边界

先观测提示并准备选择，缺少显式新版 profile 绑定时零输入失败。每次点击仍独立验证场景与目标；仅新帧证明两个选择图标都消失、进入稳定场景且不存在阻塞覆盖层时提交确认。旧代码的“Press 未生效也更新善恶值”不保留。提示未消失/出现 Retry 时报告 `karma.choice_outcome_unconfirmed`；完全未知的后置由现有门禁以 `Failed/POSTCONDITION_TIMEOUT` 结束。两者都不复点、不重启、不写回；停止/输入拒绝也不写回。

确认后先保存 Run 内操作编号、原值/新值和帧/代次，再执行新版 profile CAS。保存冲突、锁冲突、原子替换失败均结束为 `PROFILE_SAVE_FAILED`，保留已执行事实与具体存储错误，禁止自动重发。正常保存更新最新回执及 revision；同一操作回放不重复扣减。`legacy_document`、passthrough 和旧配置文件不变；`values` 才是新版有效字段权威。schema1 增加可选 `last_business_update`，旧文件缺字段不冒充已有回执。

## 当前验证

七目标构建通过（`m4-karma-confirmation-build.log`），状态组 18 方法通过，耗时 5.480 秒（`m4-karma-state.log`）。包括 19 个旧公式对照输入、真实新版文件确认前无写入、确认去重/跨代次去重及第二次选择消费最新值。

最终流程 6 方法/13 场景通过，89.270 秒（`m4-karma-workflow-final.log`，私有根 `m4-workflow-7vr_6w67`）。三种实际存储故障、未绑定/非法值零输入、停止/拒绝，以及未知/不消失/Retry 覆盖结果不能算成功均通过。配置数据组 11 方法通过，1.835 秒（`m4-karma-data.log`），补验不同大小写的旧 config 文件名均拒绝且不创建文件。所有文件只在本次 fixture 新建私有目录，SDK 与输入门禁不替换。保护基线复核 450 项，变化为零。

首轮流程 6 方法中 5 通过、1 失败（74.487 秒，`m4-karma-workflow.log`，私有根 `m4-workflow-kyqohu26`）。新增断言错误地在 Run 通用原因中寻找子流程原因；实际最后 Session 已保存具体原因，仅一次输入、无生命周期操作、profile 未变。第二轮 5 通过、1 失败（88.370 秒，`m4-karma-workflow-retry.log`，`m4-workflow-it88q5wa`），未知后置触发现有门禁的致命 POSTCONDITION_TIMEOUT，新增测试却期待普通恢复终态。两次均只纠正新断言的契约层级，不将门禁失败改成可恢复/成功；同时保留一次输入、无重启、无写回和待确认意图的强断言。初次失败不删除。实现检查点 `d5404bd`。

本批被测 EXE SHA256：`a4eda3371c986eb5886bc7e345e334697da48ceb07429ca3fcb97b5774d7bb77`。Retry 即使保留底层地下城图标，也不得作为确认成功证据。

同产物计划组 6 方法通过（82.804 秒，`m4-karma-plan.log`），包含 43 份迭代图编译/发布参数核对。通用提示、全局提示与开箱受影响回归 24 方法全部通过（1598.568 秒，`m4-karma-common-chest-regression.log`，私有根 `m4-workflow-gjfdtv0z`）。包含完整快速开箱链；该结果只属于上述善恶阶段 EXE，不覆盖其后自动地图与要塞陷阱源码。

本阶段不代表完整 TaskID、其它配置回写或跨进程业务恢复已完成，不改变 M3/M0 未决边界。
