# 项目当前事实

## 当前产品

- 旧版 Python/Tkinter 程序、`config.json`、`mod`、日志和 `dist/wvd` 未修改；原有 `next/dist/wvd-next` 也未替换。
- Windows Next 的活动服务由 `next/native/app/main.cpp` 启动，`Application` 装配唯一原生 `FlowExecutor`、MuMu/ADB/scrcpy 设备会话和 OpenCV/ORT 识别。Vue 通过同源 API 编辑配置、战斗方案和作者流程。
- 独立候选在 `next/dist/wvd-next-native`，入口为 `启动WVD原生版.bat`；数据目录为 `%LOCALAPPDATA%/WvdNext`。候选不是生产切换。
- Maa SDK 集成源码和旧阶段工具/测试已移至 `next/archive/`；当前 CMake、构建和有限验收不调用 Maa。

## 已有证据

- 锁定依赖通过哈希校验，独立目录 Windows Release 与 Vue 构建并打包。直接 OpenCV 模板、真实 ORT 英文 OCR、原生执行器、双事件退出、scrcpy/ADB 报文、公共 slot 六轮/两处调用及协调器的定向离线检查通过。
- 正式 `Application` API 在隔离数据目录完成作者流程保存、应用重开、编译、执行、根业务终点及结果落盘；同一应用入口能带 `ACTIVE_BEG_MONEY` 装配蝎女原生任务并进入 `Task_` 步骤。协调器的延期恢复可响应停止。离线后端禁止游戏输入，不表示任务实机通过。
- 候选在桌面及 390px 手机浏览器中完成工作台配置保存重开和页面切换；候选进程实际加载自身目录 OpenCV/ORT，PE 导入无 Maa，关停后端口与进程均释放。先前旧引擎在指定 MuMu 上走过繁中启动和蝎女前段，不能转记为新设备后端的实机结果。新链的设备只读验收和游戏操作验收均为 `NOT_RUN`。
- 最新候选还通过正式 API 的公共流程引用删除保护、编辑器撤销/重做/保存重开和 CaptureHost 人为挂死回收。对应证据与未完成断言统一记录在 [Maa 移除工作包执行记录](../next/docs/native-removal-acceptance.md)；产品离线验收仍为 `PARTIAL`。

## 剩余边界

- 原生任务工厂已改用项目中立的编译期图字段，发布后运行时只消费强类型 `FlowProgram`；工厂直接构造强类型步骤仍待完成。
- 原有任务的完整副作用、事件嵌套、交接/续段、停止与恢复尚未在正式新链逐项证明。尤其公会领取、入本、战斗、悬赏提交和完整蝎女任务不能宣称可用。
- 真实 MuMu 截图、Clash 状态、游戏启动及繁中页面素材仍待用户指定的有限实机范围。不得自行领取、领奖、跳轮、购买或讨伐。

使用入口为 [next/README.md](../next/README.md)，分层事实见 [架构](../next/docs/architecture.md)，执行限制见 [执行注意项](execution-notes.md)。旧阶段快照位于 `docs/archive/` 与 `next/docs/archive/`。
