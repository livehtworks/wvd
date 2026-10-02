# 素材识别方式闭环

## 范围

本轮把素材默认配方、流程显式选择、繁中OCR、坐标输出、作者保存与发布、模型打包接到现有原生链。使用已有900×1600原帧；不连接设备发输入，不开启游戏循环，不修改用户正式任务/战斗配置。

## 使用与职责

流程编辑器选择语义素材后，可选“跟随素材”或该素材当前语言登记的算法。要塞入口和第1/3/4/5/6/7/9/10区目前提供模板与OCR，默认模板。第2/8区无素材，不凭空补齐。

```json
{"mode":"semantic","id":"outskirts.fortress.zone10","method":"ocr"}
```

解析后由已有WvdVision调用正式Service：

```json
{"mode":"ocr","language":"zh-Hant","expected":["第十區-要塞3F領主室"],"match":"exact","unique":true,"threshold":0.9,"roi":[400,150,480,900]}
```

- 素材默认方式来自`variant.condition.mode`，替代配方来自`variant.alternatives`，流程只选择，不维护第二套配方；以后新增算法需在中央实现并验证，再注册真实配方，不新增空实现。
- 作者识别节点、点击目标、事件/恢复条件、诊断、序列化、缓存键保留完整参数。OCR文字框中心交给既有点击输入门禁；本轮不直接点击游戏。
- 未配置方法/语言在编译时拒绝；无匹配为NoHit；多个合格文字框为`OCR_AMBIGUOUS`且无点击坐标；模型缺失/哈希错误为Error，不换模型或降阈值。
- 同帧同语言同ROI共享一次OCR结果，不按8个目标重复推理；只保留当前帧最多4个区域、每区128框/64KiB。帧/动作代次改变即失效。证据记录模型、原文、分数、框、缓存命中、初始化和推理耗时。
- `ocr-models.json`统一锁定模型来源和哈希，准备、构建、打包、发布、运行使用同一契约。新模型按语言懒加载；英文和繁中不自动连试。下载只在prepare阶段发生。

## 验收记录

专用原生入口为`test_recognition_methods <原帧目录>`；UI/API使用隔离18758与全新临时数据目录`.local/recognition-methods-ui/data`，不使用正式配置、流程或设备连接。

| 验收 | 结果 |
| --- | --- |
| 默认模板、显式OCR、未配置方法/语言拒绝、OCR默认配方 | 通过；并验证作者编译、发布后的参数保留 |
| OCR点击目标 | 正式编译为`Input/use_target_center`，全文且唯一；非唯一配置拒绝。不向游戏发输入 |
| 已有清晰帧55与触控覆盖帧54 | 每帧8行均命中自身行中心，后7次查询复用同帧OCR框 |
| 已有入口帧46 | “不落的要塞”命中 |
| 淡入帧53、明确合成的重复名称 | NoHit、不生成目标坐标；后者保留歧义诊断 |
| 模型缺失发布、取消状态 | 拒绝发布；取消后Error，不作为NoHit消费 |
| 原模板及要塞映射 | 原22项实帧入口通过；5个旧名称映射及要塞到达条件通过 |
| 真服务/真实前端，1440×1000与390×844 | 目录含算法选项、选择OCR、保存重载、上传实帧返回第十区位置均通过；截图无新增横向溢出 |

初次UI夹具沿用了旧测试的`interface.outputs`字段，被真实接口正确拒绝；改为当前`kind/category/parameters/handoffs`契约后通过，没有放宽生产校验。原生点击检查也按现有Request封装核对`target_recognition.parameters`，不是把参数裸对象当Request。

原始证据均位于`next/.local`：`recognition-methods-native-final.log`、`recognition-methods-template.log`、`recognition-methods-fortress.log`、`recognition-methods-ui-result-fixed.log`；逐项OCR原文/框/耗时位于原生结果日志指向的隔离`results.json`。UI截图在`next/web/test-results/recognition-methods-*/recognition-methods.png`。临时18758服务已由正式Stop接口关闭，实例停止记录`recognition-methods-ui-stop.log`。

交付候选为`next/.local/c11-flow-product/candidate68`，使用管理脚本Deploy替换原17654空闲服务，不开启循环。部署以`recognition-methods-deploy.log`及`recognition-methods-deployed-state.json`记录的实际PID/实例、Idle状态、profile前后哈希为准；候选`DELIVERY_STATUS.json`保存源码/构建/包内模型身份。未提交或推送源码，本轮未收到此要求。

## 边界

OCR清晰名单的试验速度约0.8～0.94秒一次，全名单缓存能摊薄多个查询，仍比模板慢，因此不全局改为OCR。淡入帧可能识别不出；由原流程继续等待新帧，不用NoHit伪造点击或任务完成。测试只证明本批画面的配方和目标坐标，不能宣称完整要塞赏金自动路线通过。原`MEMORY_UNATTRIBUTED`独立保留。

繁中识别模型文件约80.7MiB。推理引擎按需初始化，但既有BundleLease仍持有包成员的已校验原始字节，包缓存/发布快照因此有额外内存和磁盘成本；不能把“懒加载引擎”说成“未使用OCR时零成本”。本轮没有另改资源租约或宣称历史内存问题已解决。
