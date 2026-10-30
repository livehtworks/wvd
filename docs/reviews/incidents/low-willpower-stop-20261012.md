# 低意志力提示停止规则

## 现场与边界

用户明确这是反复死亡后的低意志力警告，不是多人死亡救人页。要求识别该提示、保存截图、停止整个循环，不再等待60秒重启；修好后不启动循环。

原现场为563E32FD-B980-4FDC-A473-48244D2F0761/run11。Task_FirstEntry_Step1Found@await被提示挡住，事件564等待59402ms，事件565主动bound_game.restart，事件566的原图仍是同一提示。后续ADB/实例恢复不证明模拟器退出根因。原回执和两张警告图保存在next/.local/low-willpower-stop-20261012/；这批run1–10保存Completed，累计39/100，run11不计成功。

## 实现与来源

- OCR指从截图读取文字，使用项目已有繁中模型与Service，模型权威为next/resources/recognition/ocr-models.json；没有新增模型、英文猜译或伪造模板。
- 识别配方在next/native/games/wvd/vision/location_probes.hpp的low_willpower_warning。要求“意志力較低迷”和“對復活產生重大負面影響”两段正文同时存在，不限定角色名字，不以通用“確認”按钮判定。900x1600视口下搜索全宽中央800像素，原图已验证。
- 同一停止声明在next/native/games/wvd/recovery/low_willpower.hpp的preserve_low_willpower_stop。入副本和启动/公共覆盖页检查复用该声明，不新增点击处理器、重规划或外部监控。
- 只接在入副本与启动检查的异常分支，正常当前步骤命中后不执行该OCR检查；未把它加入旅店付款、战斗技能或正常导航的每次点击前置条件。入副本后续选择节点与生成的Await均保留声明，不只保护Entry。
- ExternalBlocked是现有执行器的等待人工处理终态，不是新造的行业术语。原因代码为party.low_willpower_manual_required。现有FlowExecutor、Session、Coordinator及Application负责终止、保存原帧和结束批次；不把终态当作完成轮次，不重启或续跑。
- 未决输入保留原账目，不补点、不伪确认。实际救人、多人死亡和再起职责未改。

## 验证

- 最终next/.local/lw-c2-261012/receipt.json：app/recognition共31个入口complete=true，包含新增低意志力入口、Sent/Unknown完整账目保留、启动和公共覆盖页停止。这里不是31轮游戏。
- next/.local/lw-real3-261012/receipt.json及results/results.json：8个真实Service/原流程/执行器组合场景通过，只有设备传输隔离，没有操作游戏。
- 当前警告原图：2204ms，停止，输入0；重启前警告原图：2141ms，停止，输入0。
- 普通选层页点击后出现警告：3133ms，停止，仅原选层输入1次，pending保留，没有额外输入。
- 已在警告页启动检查：4429ms，停止，输入0。
- 普通选层、迷宫、角色面板均NoHit；英文识别模式不执行繁中警告识别。英文警告未取得原帧，不宣称英文警告已覆盖。
- 首次离线图验证因新增规则priority=1100超过既有0–1000合同失败；已改为1000，未放宽合同，失败日志保留在next/.local/lw-real-261012/及logs/lw-real-frames-20261011T172321328199.log。
- 上述最终两个回执绑定相同产品输入8324c1cc25ffe341b26e037be983803da602a6d0cf7c0b3080beeaf0a9c4782a/1300项。

## 交付

正式候选通过原build.py/package_functional.py/manage_service.ps1链构建部署，身份、空闲状态和用户配置逐文件核对记录在next/.local/lw-build-261012/及deployment.json。未进行实机新循环，不以离线耗时冒充实机时延或100轮稳定性；用户停止优先，定时检查保持暂停。内存仍RESOURCE_UNRESOLVED。本轮未自动commit/push。
