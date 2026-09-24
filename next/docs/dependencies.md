# 当前固定依赖

当前产品使用 C++20 / VS 2022 / CMake、Boost 1.90.0、nlohmann/json 3.11.3、OpenCV 4.12、ONNX Runtime 1.22.1、scrcpy server 3.3.4、Vue 与现有前端锁文件。`next/dependencies.lock.json` 仅锁 Boost、JSON 和工具链；`next/native-dependencies.lock.json` 锁原生库、模型、服务端文件及 SHA256。`next/web/package-lock.json` 锁前端依赖。构建脚本会验证下载文件的摘要，不用系统 PATH 中的其他 OpenCV/ORT 满足运行依赖。

发布目录只带项目自己的服务/CaptureHost 与锁定的 OpenCV、ORT、scrcpy server、资源和前端。MuMu 安装中的 IPC DLL 只由自有 CaptureHost 在用户选定实例下加载，不从旧 Maa SDK 获取。第三方声明见 [third-party-notices.md](third-party-notices.md)。旧 MaaDeps 锁和旧阶段依赖说明已归档，不能再作为当前构建的版本权威。
