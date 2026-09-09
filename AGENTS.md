# 项目画像

- 本仓库是巫术 Daphne 自动化工具的个人维护 fork，技术底座为 Python、Tkinter、OpenCV、ADB 和 MuMu IPC。
- 主入口与任务线程生命周期在 `src/main.py`；配置面板在 `src/gui.py`；截图、任务和恢复状态机在 `src/script.py`；日志与资源在 `src/utils.py`。
- 运行目录的 `config.json` 是用户配置权威，`mod` 是既有用户扩展；打包不得覆盖配置或清理运行日志、mod。
- 识别基准为英文游戏界面、900x1600 像素；模拟器路径由配置定位。
- `origin` 指向原作者，`fork` 指向个人仓库；只有明确要求时提交或推送，推送明确指定个人远端。
- 当前事实入口：`docs/project-status.md`。执行构建、测试或诊断前读取 `docs/execution-notes.md`。
- 本地构建：`用于本地测试的打包脚本.bat`；环境 `.venv-build`；输出 `dist/wvd/wvd.exe`。
- 离线回归：`.venv-build/Scripts/python.exe -m unittest discover -s tests -v`。测试应使用独立临时目录，避免 GUI 保存配置或导入时日志清理影响用户现场。
