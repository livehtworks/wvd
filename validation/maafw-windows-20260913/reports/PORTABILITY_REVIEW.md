# Spark / Linux ARM64 静态边界

状态：OUT_OF_SCOPE（运行）。没有连接 Spark，没有部署，没有运行 Linux 用例。

## 已核对的固定来源

- MaaFramework v5.13.0 官方发布清单包含 Linux aarch64 资产；这只证明存在分发，不证明适配目标 Spark。
- 核心探针使用 C++20、标准库和 Maa C ABI；图像计算通过原生 Maa/OpenCV 路径。
- Windows 进程内存、句柄、线程统计和模块路径查询集中在 adapters/windows/platform.cpp。
- 真实设备控制实现当前位于 adapters/windows/device.cpp；MuMu 专有 DLL / IPC 必须留在 Windows。
- 当前 CMake 明确是 Windows 验证工程，链接 .lib、psapi 并嵌入 UTF-8 manifest；不能声称现成支持 Linux。

## 迁移时必须重新证明

- Linux aarch64 的 ABI、标准库、动态库解析、线程模型及 headless 资源加载。
- Linux 大小写敏感：Inn.png 与 inn.png 不等价，保持资源实际文件名。
- Linux 的内存与对象增长指标需用该平台的可比较测量方法；不得复用 Windows 句柄数。
- 中文路径在 Linux 通常按 UTF-8，但仍需重复相同资源/图片测试。
- Android 连接方式与实例所有权由目标部署确定，不复制本机 MuMu 路径和 loopback 地址。
- ONNX Runtime provider、驱动、模型算子及精度需单独验证。存在 ONNX 或 Linux 二进制不等于 Spark GPU 已可用。
- 不把这次 Windows IPC 的 11–13 ms 当作 Linux、远程 ADB、GPU 或真机性能承诺。

固定来源：
https://github.com/MaaXYZ/MaaFramework/releases/tag/v5.13.0
https://github.com/MaaXYZ/MaaFramework/tree/2bcfa85c66a2eac6ca3e5937f175495275ee0643

