# 第三方依赖与素材来源

本地 Windows 候选固定以下版本；实际文件 SHA256 见 `native-dependencies.lock.json`，构建脚本在复制前校验。历史 Maa SDK/MaaDeps 不在当前构建或候选中。

| 组件 | 本轮用途 | 许可证/来源 |
| --- | --- | --- |
| OpenCV 4.12.0 | 模板识别、图片解码与缩放 | Apache-2.0；[OpenCV 仓库](https://github.com/opencv/opencv/tree/4.12.0) |
| ONNX Runtime 1.22.1 | 本地 CPU OCR 推理 | MIT；随候选保存上游 `LICENSE` |
| scrcpy server 3.3.4 | Android 控制协议服务端，不使用桌面视频/音频界面 | Apache-2.0；[scrcpy 仓库](https://github.com/Genymobile/scrcpy/tree/v3.3.4) |
| RapidOcrOnnx core `abd498c` | OCR 前后处理源码 | Apache-2.0；项目内 `third_party/rapidocr_core/LICENSE` |
| PP-OCR v3 英文模型 | 原有资源的精确哈希封存，当前只用于英文 OCR | 本地 `det.onnx`、`rec.onnx`、`keys.txt` 的 Git blob SHA 与 [MaaCommonAssets 固定提交](https://github.com/MaaXYZ/MaaCommonAssets/tree/dabcd4681ac990dc4361de26416d986abd80e4aa/OCR/ppocr_v3/en_us) 的对应文件一致；该仓库 [MIT 许可证](https://github.com/MaaXYZ/MaaCommonAssets/blob/dabcd4681ac990dc4361de26416d986abd80e4aa/LICENSE) 保存在 `resources/ocr/LICENSE-MaaCommonAssets`。模型基于 PaddleOCR；[上游项目](https://github.com/PaddlePaddle/PaddleOCR) 使用 Apache-2.0。 |

候选的 `licenses/Apache-2.0.txt`、`licenses/ONNX-Runtime-LICENSE.txt`、`licenses/MaaCommonAssets-MIT.txt` 保存相应协议文本。本文件记录实际构建依赖与来源，不替代各组件正式许可证或法律审查。
