# M2 离线识别单元：实现与验证

> 本文封存前一次识别单元的范围与结果，不再作为当前 M2 完成度依据。
> 后续完整核心及本轮验收见 [M2 核心验证](m2-core-validation.md)，原有结果不改写。

日期：2026-09-13。前置工作包 `WVD-M1-FIX-a03f15d` 已独立完成，见
[M1 收尾报告](m1-fix-validation.md)。用户最新请求授权通过后继续推进，因此本单元属于后续 M2，
不是把 M2 实现混入或追写成 M1 验收。

**结论：M1 工作包完成；M2 已推进并通过首个独立识别单元，完整 M2 尚未完成。**
本次没有执行新的 commit/push、打包旧版或切换生产。

## 实现边界

`contracts` 保存无 SDK 指针的帧身份和 Observation；`maafw` 负责真实 C ABI、资源所有权及详情解析；
`preflight` 负责图片、资源和参数；Windows 平台层只实现系统 SHA256。没有把 WVD 任务塞进一个 Custom，
没有新节点解释器，没有假 RunCoordinator 或未接入的运行 API。

实际调用链：

```text
独立样本 -> FrameEnvelope + RecognitionRequest
 -> 帧/资源/参数预检
 -> MaaTaskerPostRecognition -> 等待自身任务结束
 -> TaskDetail -> NodeDetail -> RecognitionDetail
 -> Hit / NoHit / Error（框、中心、分数、原生 ID、错误阶段）
```

离线对象只创建 Resource 和 Tasker，**不创建 Controller**。锁定版本源码明确允许不绑定 Controller；
SDK 实测也成功完成识别。M1 `automationd` 不链接识别库，API 能力保持 M1 只读。
页面页脚仅把已失效的“M2 未启动”改为“任务执行未接入”，没有新增控制按钮或改变接口语义。

## 关键行为

- OCR 与模板共用三态结果，不存在 Unset；不通过业务 Completed、空框或 false 单独推出 NoHit。
- NoHit 要求有效资源/参数、原生识别任务成功、单节点详情及 all/filtered 结构一致；OCR 使用真实识别文字与 score。
- 完整资源清单逐文件核对 SHA256，未列出的资源、缺文件、运行期间文件变化及路径越界拒绝。
- 模板必须可解码、尺寸不超 ROI；阈值只接受有限的 0..1；不为通过测试放宽阈值。
- OCR 目前只接受锁定英文模型与字面预期文字；用户文字转义后交 Maa，不开放任意正则。
- 帧身份包含设备/游戏/包/代次/帧号/动作序号/viewport/尺寸/单调时间/颜色。与调用者提供的当前上下文不符时拒绝。
- 三态中 Error 的命中框、中心和候选全部清空，错误不成为下一次点击依据。
- 原旧版所有模板、ROI、识别策略和 OCR 能力保持原状；此处不是旧功能迁移完成或退役授权。

## 依赖和隔离

- Maa 5.13.0，commit `2bcfa85c66a2eac6ca3e5937f175495275ee0643`。
- SDK 压缩包 SHA256 `179b51b6211fc74317c8dc06f10fae4724aca370df7d1bf694cd00d06ff38efe`；准备阶段校验并复制 176 个文件，构建和测试再次核对文件 hash。
- OCR 使用既有锁定 commit `dabcd4681ac990dc4361de26416d986abd80e4aa`、`ppocr_v3/en_us`，3 个文件逐项复核，显式 CPU provider。
- SDK、模型、原生日志和生成图片只在被忽略的 `next/.local`。准备工具参数显式指定来源，没有提交私人路径或硬编码模拟器位置。
- 每轮测试生成独立目录和中文资源路径；图片由固定种子的噪声、模板块及文字组成，不读取真实游戏图片，不连接设备，不调用旧 Python 模块。
- 测试专属资源被有意写坏以验证错误路径；没有写用户 config/mod/旧日志。最终 450 个受保护文件 hash 全部未变。

## 验收结果

最终 `next/tools/validate.py --m2-offline` exit=0：
**23 项 M1 原生服务/盘点测试 + 29 项 M2 识别测试 + 4 个真实浏览器场景全部通过。**
移动端、桌面端刷新截图已目视检查；全部测试进程正常结束，没有超时清理。

29 个 M2 用例产生 33 次 Observation：7 Hit、3 NoHit、23 Error。
多出的 4 次来自同一个识别对象重复识别 5 次，task_id/reco_id 均不同，不是额外游戏周目。

| 断言 | 结果 |
| --- | --- |
| 模板真实命中与无命中 | PASS；命中框 `[334,417,80,40]`，中心 `[374,437]` |
| OCR 真实命中 | PASS；`Pause`，框 `[285,630,250,69]`，score `0.999084`，outcome=Hit |
| OCR 不符合预期、字面 `.*` 不被当作正则 | PASS，均 NoHit，有效原生 reco_id |
| OCR 缺模型、坏模型、空预期 | PASS，Error，未启动原生识别任务 |
| 坏帧、坏模板、缺模板、模板大于 ROI | PASS，Error |
| 非法阈值/ROI、目录穿越、hash 不符、未列资源、加载后变化 | PASS，Error |
| 旧代次/帧号/动作序号、其他设备/包/viewport/颜色 | PASS，Error |
| 未来时间、尺寸/比例不一致 | PASS，Error |
| 连续 5 次实际识别与中文目录 | PASS，进程正常返回 |

Error 路径全部检查了 outcome、错误码、错误阶段、空命中和 `task_id=0`，不是“程序没崩就算通过”。
本次合成 OCR 的 Hit/NoHit/Error 是新增适配证据，**不修改 M0 的 outcome=Unset 历史结果**。

## 实际遇到的问题

1. 首轮 25 项测试均在驱动 JSON 解析处报错：弃用的 `MaaSetGlobalOption` 向 stdout 输出警告。
   实际 OCR 原始结果已为 Hit，但没有据此跳过验收；改为 `MaaGlobalSetOption`，结构化结果单独写文件，完整 stdout/stderr 保留。
   第二轮 25 项全部通过。
2. 后续格式化把 bcrypt 排在 windows 头前，完整构建失败。固定依赖顺序后原生构建通过且没有编译警告；
   追加 4 个边界用例，最后执行上述 29 项与完整 M1/浏览器回归。

失败日志均保留，没有改写成首次通过，也没有修改 M1 已冻结的修正轮次。

## 证据索引

- 最终用例：`next/.local/m2-runs/recognition-zfvsunhi/results.json`；每个子目录包含输入配置、样本、原生结果及 stdout/stderr。
- 结果文件 SHA256：`86aabde62c18d754bf3b2ad1254b965747306b34818a6e2ae08eeef2bffb86f4`。
- 原生驱动 SHA256：`d90bcdda3765e2c0e0aa175dde03925c39084db74391dedd9df8220b8118c3f9`。
- 识别库 SHA256：`e412734a8290c9a32c48ac13fe4cd2c76e32a7b73ba8bae04bfa50d66484b4e5`。
- 构建：`next/.local/m2-final-build.log`（保留顺序错误）、`m2-final-native-build.log`（修正通过）、`m2-final-web-build.log`；之后 `m2-final-build-confirmed.log` 完整 build.py --m2-offline 再次 exit=0，驱动 hash 未变。
- 验收：`next/.local/m2-final-validation.log`；详细日志 `next/.local/logs/m2-recognition-tests.log`、`native-inventory-tests.log`、`browser-tests.log`。
- 早期结果：`next/.local/m2-recognition-round1.log`、`m2-recognition-round2.log`。

本地原始路径和样本不发布；源码与脱敏说明可审核，但不把未附带原生运行环境的审核称为复测。

## M2 剩余工作与禁止推论

本单元只验证固定模板/OCR 输入和上述预检错误，不覆盖所有 SDK 内部故障、任意模型、复杂正则及真实业务样本。
调用者提供的 current 身份不等于已经实现实时场景观察；没有经过 InputGate，所以不能声明完整帧到输入安全链通过。

原 M2 仍需按原设计完成：

1. RunCoordinator 与有限 ExecutionSession：重复启动、代次隔离、恢复原因、单一所有者与会话清理。
2. MaaGateway 的回调/子任务结果和根业务终态；原生成功与业务完成分离，终态保存一次。
3. GuardedAction/InputGate：全部输入尝试可观测、停止立即关门、后置条件、设备串行与许可。
4. 正常停止与 STOP_TIMEOUT：未静止不能宣布 Stopped，保留占有并拒绝再次 start；禁止强杀线程或隐藏回退。
5. 有界事件、原子结果落盘及上述真实 SDK 契约回归。

当前离线对象同步等待自身任务，没有实现生产取消；测试外部 90 秒超时只用于判失败，不能转为生产方案。
本次没有重复 M0 内存测试，`RESOURCE_UNRESOLVED`、原生阻塞取消、真实 NEXT/Pause、Spark 和生产切换限制全部保留。
**不得进入 M3 实机或宣称完整 M2 / 生产替换通过。**
