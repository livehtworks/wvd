# T0事故报告：共享模型硬链接耗尽导致任务无法启动

日期：2026-10-25。诊断快照时间：14:54（Asia/Shanghai）。该快照当时状态：原因已定位，尚未修复。

2026-10-26状态索引：下文保留原始诊断事实。随后硬链接容量处理及相关六包修复已完成并部署，实测1024总链接边界及范围见[修复验收](t0-lifecycle-repair-20261025.md)，不以原始1023快照代替后续补证。发布历史与测试产物累积另立[T0生命周期事故](t0-storage-lifecycle-20261026.md)，整改仍未完成；前次任务恢复不等于本次事故已关闭。

## 事故与影响

巨人悬赏的自动续轮和手动启动共用原生资源发布链。共享OCR检测模型的硬链接数达到1023后，准备下一份任务资源包失败，返回：

```text
NATIVE_BUNDLE_LINK_FAILED:NTSTATUS=3221226085
```

最新失败请求为 `b46c7a17-96d5-4121-b330-185896e03cf7`，任务为 `GiantBounty`，语言为 `zh-Hant`，请求目标为200轮。工作流编译已完成，失败发生于发布资源的 `file_copy` 阶段，尚未为该请求启动新run。诊断时没有活跃循环：submission为failed，repeat为inactive/stopped，busy=false、quiescent=true；最后run94为Completed、三业务单元、result_saved=true。

此问题阻断新任务准备。界面上的新失败请求显示completed_cycles=0，不代表此前保存的成功回执丢失。此前设备重连问题已经通过原服务的device/disconnect→connect解决，与本次发布失败分开记录。

## 已证实原因

十进制NTSTATUS `3221226085` 等于 `0xC0000265`，对应 `STATUS_TOO_MANY_LINKS`。微软将其定义为创建的文件链接超过文件系统支持数量；硬链接接口文档给出的每文件上限为1023。

- [微软FileLinkInformation错误定义](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/69643dd3-b518-465d-bb0e-e2e9c5b7875e)
- [微软CreateHardLinkW限制说明](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinkw)

真实调用链为：

```text
Application任务准备
  → NativePublisher发布本次资源包
  → 复用published/asset-objects/<hash>/payload.onnx
  → BundleLease::link_member
  → NtSetInformationFile(FileLinkInformation)
  → STATUS_TOO_MANY_LINKS
```

[NativePublisher](../../next/native/games/wvd/tasks/native_publisher.cpp)对非模型资源复制原锁定文件；对ONNX模型，先按内容哈希建立不可变对象，之后每次发布均从该对象创建硬链接。[BundleLease::link_member](../../next/native/platform/windows/bundle_lease.cpp)在系统调用失败时只抛出该NTSTATUS，现有路径没有处理共享对象硬链接额度耗尽。

冻结包中 `model/ocr/det.onnx` 和 `model/ocr/zh-Hant/det.onnx` 指向同一检测模型哈希。因此，每份包含这两个成员的资源发布会为同一个物理模型文件增加两个硬链接。历史发布目录持续保留，链接额度随发布累积；重复点击开始或重启进程不会降低已有链接数。

诊断时只读取得的模型对象信息如下，路径均位于正式data-root的 `published/asset-objects/<模型键>/payload.onnx`：

| 模型键（SHA256） | 文件大小/字节 | 硬链接数 |
| --- | ---: | ---: |
| `8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9` | 2423224 | 1023 |
| `da12c6e863761d774b07d3bd40fbaaa55516f90570e0b1dd9dc112e457301cc9` | 8956702 | 512 |
| `e09385400eaaaef34ceff54aeb7c4f0f1fe014c27fa8b9905d4709b65746562a` | 84577022 | 512 |

系统实际返回的超限错误、模型链接数及发布代码共同证实本次根因；没有将其归因于MuMu内部崩溃、内存趋势或缺少识别素材。

## 已保存轮次与排除项

依据每个run的原始请求身份校验归属，派生repeat请求按原session与cycle哈希逐份核对。完整成功要求Completed、三业务单元、result_saved/quiescent、无storage_error，并核该轮bounty_cycle已完成且无待交账或待住宿付款。

| 根请求 | run范围 | 原目标 | 完整成功 | 排除项 |
| --- | --- | ---: | ---: | --- |
| `giant-200-resume-20261025-9dfea98c3b21429d8018e359838bb7da` | 2–72 | 200 | 70 | run72为UserStopped/STOP_REQUESTED，仅1业务单元 |
| `75f3dc44-1896-4e66-a3ce-ba69dc7bab99` | 73–94 | 200 | 22 | 无 |

共核实92份完整成功回执，按两份根请求分别记账，不自行合并成同一200目标。更早的40轮及独立100目标不加入这两段。最新失败请求没有新run，也不计成功。

## 现场与证据

- 诊断时服务实例：`BAA009E9-6584-4349-86B9-D5FDE0F4882C`；PID22208，process_start_filetime为`134370861777540010`。这些是诊断快照，后续操作必须重新核验实际身份。
- 服务端口：17654；正式data-root：`D:/programcode/python/wvd/next/.local/c11-flow-product/data`。
- 当前候选：`next/.local/rescue-marker-candidate-261020/automationd.exe`；仓库诊断基线：`ee8dd8c53f1d37606753ed8d94c9529e40c3b43e`。
- 本地诊断：`next/.local/bundle-link-check-261025-065401-1b573c/diagnosis.json`，包含错误、当前请求状态、三份模型对象链接数、92份成功回执的完整路径/SHA及run72排除项。
- 原启动与设备重连证据：`next/.local/giant-200-resume-261025/`；成功启动请求以`retry-ledger.json`和`start-confirmation.json`为准，首次连接失败另有保留。

本报告保存可独立审阅的结论及证据索引；本地运行日志、原图、转储和正式数据不随报告上传。

## 处理边界与剩余工作

本轮仅完成诊断和回执审计，没有修改生产代码、删除历史发布目录、改写配置或旧回执、重启服务/模拟器，也没有重新提交任务。定时监控保持暂停。

后续修复需要使模型发布与复用在长期运行下不再耗尽单个文件的硬链接额度，同时保留冻结哈希校验、原锁定语义、历史发布可读性及失败回滚。验证须在全新可丢弃目录证明超限场景与正常发布，不能在正式模型上继续建测试链接或通过删除正式历史绕过故障。修复、验证和部署尚未完成，不将此报告作为已恢复运行的证明。
