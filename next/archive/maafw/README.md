# Maa C ABI 适配层（M2）

M1 服务仍不链接或加载 Maa；锁定版本见 `../../dependencies.lock.json`。
`MaaGateway` 是唯一 SDK 适配和对象所有者。`ExecutionSession` 与无 Controller 的
`OfflineRecognizer` 共用该实现；不复制探针的全局布尔状态，不保留另一套执行器。

- `contracts/recognition.hpp`：无 SDK 指针的帧身份、识别结果三态、框/中心/分数/错误阶段。
- `recognition.hpp/cpp`：串行识别及 TaskDetail -> NodeDetail -> RecognitionDetail 三态转换；Custom 内直接识别也共用转换。
- `gateway.hpp/cpp`：Resource/Controller/Tasker 所有权、Custom 注册、事件、子任务状态与停止映射。
- `guarded_controller.hpp/cpp`：覆盖所有 SDK 输入回调，转发到同一 InputGate，捕获跨 C ABI 的异常。
- `preflight.hpp/cpp`：完整资源清单及 hash、路径、图片解码、ROI、参数和固定 OCR 模型核验。
- `buffers.hpp`：SDK 图像/字符串缓冲区的唯一所有权；没有第二套识别计算。
- `platform/windows/file_digest`：Windows 系统 SHA256，不发 shell 命令。

只有输入和资源有效、原生识别成功、详情一致时才能 NoHit；Hit 返回实际 SDK 框/分数。
OCR 预期文字按字面量转义，不开放任意正则，也未把旧 WVD OCR 参数迁入此接口。
该限制是独立适配单元当前范围，不修改或退役旧能力。

完整 Session 的正常释放顺序为 Tasker -> Controller -> Resource -> 回调数据，完成后才能报告 quiescent。
无 Controller 的独立识别复用同一 Gateway，只没有设备对象。原生等待发生在工作线程，
不能放进 HTTP I/O 线程；监督线程负责关门禁与 STOP_TIMEOUT，但不声称任意原生阻塞可以取消。
实际设备实现属于 M3，目前 DeviceBackend 的具体实现仅存在于测试。
