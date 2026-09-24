# 当前执行注意项

- 每轮先界定唯一用户功能和最短验证路径。构建、Mock、离线流程、接口 2xx 均不等于真实游戏任务成功；不跑旧全任务矩阵、资源长测或重复失败用例挑成功。实机副作用必须遵守当轮明确范围。
- Windows 中文文件与输出使用 UTF-8。长构建日志写 `next/.local/logs`，只检查退出码和关键摘要；不要按截断输出下结论。
- 新版唯一构建入口是 `python next/tools/build.py`，有限离线入口是 `python next/tools/validate.py`。Maa 准备脚本和 M2/M3/M4 旧验收已归档，不得作为当前接线依据。
- CMake 通过 PATH 或 VS 2022 的 vswhere 定位；Node/npm 版本按 `next/dependencies.lock.json`。OpenCV、ORT、scrcpy 文件按 `next/native-dependencies.lock.json` 校验。系统 PATH 中旧 ORT 可能抢先加载，构建与测试可执行文件旁必须放锁定 DLL。
- 将全新 MSVC 构建目录放在 `%TEMP%` 会出现 `MSB8029` 增量构建警告；本轮 Release 仍通过。常规增量构建使用 `next/build`，独立验证另建目录并保留完整日志，不把该警告误判成编译失败。
- 不覆盖旧 `src/`、`config.json`、`mod`、日志、`dist/wvd`、`next/dist/wvd-next` 或 `.vscode`。原生候选使用独立 `next/dist/wvd-next-native`；调试应用一律显式传隔离 `--data-root`。
- 不要在正在运行的本目录 `automationd.exe` 上重新链接；按进程路径、命令行和端口确认后正常停止，不结束全局同名进程。构建完成后核对候选 exe 和依赖来自当前锁定归档。
- 旧 Python 源码只读参考，不能 import 旧 `utils`/GUI 做 Smoke；它可能初始化日志或保存用户配置。历史 Maa 源码和证据保留在 `next/archive/`、`docs/archive/`，不参与当前编译。
- 模板识别必须区分 Hit/NoHit/Error。OCR 当前锁定英文模型，不冒充繁中识别。图标点击中心只定位入口，不能用通用城市图标推断王城；王城塔楼背景是只读地点证据。
- 游戏启动/VPN 需分别观察前后状态，不要求一次点击两秒生效。NoHit、黑帧、缺资源和网络慢不能升级为模拟器重启。已发送但结果不确定的非幂等输入不得重发。停止只释放本工具自有输入/进程。
- 本阶段候选的设备只读和游戏流程验收尚为 `NOT_RUN`；若要执行必须有明确范围，不从旧 Maa 的实操记录推断新链成功。
