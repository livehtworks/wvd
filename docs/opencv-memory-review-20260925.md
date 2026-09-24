# OpenCV 分配失败与内存用法复核（2026-09-25）

## 结论先行

**不能依据现有日志认定“内存泄漏”或 OpenCV 用法已经安全。**现有一条真实故障证明：OpenCV 4.12.0 在一次识别中申请 `34,620,024` 字节（约 33.0 MiB）失败。当前实现有较高的**瞬时峰值风险**：最多四路候选匹配并行、每路 `matchTemplate` 有自己的结果/内部临时缓冲，模板/遮罩缓存只限制条目数，不限制解码后字节数。但旧记录没有故障时的模板、ROI、并发分支、私有提交量或调用栈，无法把那 33 MiB 归到某个具体缓冲，更不能外推泄漏速度。

## 事故的原始证据

- 本机只读记录：`next/.local/real-chest-zh-hant-20260924/data/runs/96B91D0E-279E-4705-A7D2-4A166623629E/1/{run,result}.json`；旧发布版本 `5fc0875df641916919cc976e02d55c57ebbc2bf00d05a1e15a3aba7a329dc3b3`，900x1600。
- `result.json`：运行约 24.15 秒，终态 `Failed`，`inputs.attempted=accepted=backend_called=0`，最后来源节点为 `Task_Author_OpenChest_Business_Entry`。OpenCV 原文是 `alloc.cpp:73 ... Failed to allocate 34620024 bytes in cv::OutOfMemoryError`。
- 对应旧发布图中，该入口是 route，先评估 `blocking_screen` 中断候选，再进入业务确认。`blocking_screen` 本身按顺序检查，但其中默认对话候选可能触发四分片批处理。**这是可达链分析，不是该异常的已证实调用栈。**
- 这条旧结果没有识别参数 ID/模板名、实际 ROI 和尺寸、异常前内存数值或堆快照。OpenCV 报的是**一次申请的大小**，不是进程已占内存、累计泄漏量或当时剩余物理内存。

## 当前代码的内存路径

1. `recognition/frame.cpp::FramePixels`：MuMu 原始 BGR 帧通过 `shared_ptr` 借用，不再深拷贝一张；编码帧则 `imdecode` 成 BGR。`recognition/service.cpp` 只保留当前帧的 `FramePixels`，换帧时替换。900x1600 BGR 像素本体为 4,320,000 字节（约 4.12 MiB）。`FrameEnvelope` 的编码 PNG 字段是值类型，若走编码路径，某些帧副本仍可能复制压缩字节；不能把“像素共享”推广到所有后端。
2. `recognition/service.cpp::evaluate` 换帧仅清 `cache_.results`，**不清 `cache_.assets`**；后者在 Service/会话生命周期内保留解码模板、遮罩和少量跨帧样本。现有限额是 `<2048` 个条目，不是字节预算。资源包有 539 张 PNG，压缩文件总量约 13.9 MB；实际解码持有量不能由此计算。
3. `games/wvd/vision/native_recognizers.cpp::evaluate_batch` 为最多四个分片创建独立 `Cache` 和 `EvaluationMemo`，用 `cv::parallel_for_` 分配候选。复制到 worker 的 `cv::Mat` 通常只复制引用计数/头部，**不能说模板像素被深拷贝四份**；但每个并行匹配仍会独立持有自己的结果与 OpenCV 内部临时缓冲。默认对话等批处理即使前面的候选命中，也先计算整个批次。
4. 同文件 `match()`：全帧模板匹配的 32 位单通道结果图理论上可接近 5,760,000 字节（约 5.49 MiB）；`grayscale` 额外转换搜索图，`exclude` 明确 `clone()` 搜索 ROI，亮度遮罩分支还会构建遮罩并走 masked `matchTemplate`。ROI 小时这些分配相应缩小，旧故障的有效 ROI 未知。
5. OpenCV 4.12.0 [模板匹配文档](https://docs.opencv.org/4.x/df/dfb/group__imgproc__object.html)规定结果为 32 位浮点、大小 `(W-w+1)×(H-h+1)`；[cv::Mat 文档](https://docs.opencv.org/4.x/d3/d63/classcv_1_1Mat.html)说明普通 Mat/ROI 拷贝共享数据，`clone()` 才深拷贝。其 [4.12.0 实现](https://github.com/opencv/opencv/blob/4.12.0/modules/imgproc/src/templmatch.cpp)对 masked 路径包含转 `CV_32F`、中间结果等分配，普通路径也有相关计算/临时缓冲。**仅凭 34,620,024 这个数无法确定走了哪条实现或哪一个矩阵。**

## 优先请复核的风险

| 优先级 | 代码位置 | 需要判断的问题 | 当前证据 |
| --- | --- | --- | --- |
| 高 | `native_recognizers.cpp::evaluate_batch` / `default_dialogue` | 四路候选同帧执行是否使模板匹配峰值叠加；能否保持错误优先级而减少无关候选/限制并发 | 有可达路径，缺事故时分片实测 |
| 高 | `recognition::Cache::assets` / `AssetResolver::load` | 2048 条目但无限字节，会话内模板/遮罩可达到多少解码占用；缓存是否有跨任务会话残留 | 生命周期代码可见，无增长曲线 |
| 中 | `match()` 的全屏、`exclude`、灰度与 masked 分支 | 哪些实际业务条件缺必要 ROI；是否在同帧重复生成大缓冲 | 代码可见，事故具体参数未知 |
| 中 | `FlowExecutor` 的帧持有 | 编码帧按值复制可能保留多少压缩数据 | 取决于当时后端，旧记录不含该事实 |
| 诊断 | `native_recognizers.cpp::one()` 的异常包装 | 新版附模板、尺寸、分片、缓存项数和进程私有提交/工作集，但只覆盖进入 `one()` 后的异常；真正内存紧张时构造 JSON 也可能再次失败 | 新代码已构建，**未在同类真实异常中验证** |

## 不应据此做的判断或改动

- 不把 `cv::Mat`/worker 缓存的浅拷贝误称为四份图像深拷贝；不把单次 33 MiB 申请误称泄漏。
- 不因这条记录直接关掉所有并行、下调识别阈值、缩放截图或重启模拟器；这些会改变识别语义或掩盖故障。
- 不开展长时间内存矩阵或重复实机尝试来“碰到一次成功”。下一次用户自然运行若再遇到同类故障，只读这一条新版诊断，先对照模板、ROI、分片、缓存项与私有提交，再做针对性修改。

## 建议 GPT 审核的问题

1. 在锁定的 OpenCV 4.12.0 Windows 构建中，`TM_CCOEFF_NORMED` 与 masked `TM_CCORR_NORMED` 对 900x1600 BGR 输入各有哪些大块临时分配？`34,620,024` 字节能否据实际尺寸/实现定位；若不能，请明确不能。
2. `cv::parallel_for_` 的四个外部分片与所用 OpenCV/IPP 内部并行在本构建是否会叠加峰值？不要仅从注释推断串行。
3. 对 `Cache::assets` 应采用解码字节预算、按会话清理或更窄的模板集合中的哪一种；如何不破坏同帧复用与素材别名/mod 优先级？
4. 如何在 OOM 时以低分配开销记录参数/来源，并保证识别 Error 不降成 NoHit、不会重复提交游戏输入？

本报告只做代码和既有 Run 的只读复核；未连接 MuMu、未运行游戏、未复现分配故障。相关当前阶段状态见 `docs/repair-8f61540-closure.md`。
