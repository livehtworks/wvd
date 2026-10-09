# 前台切换与重复重启修复

本轮用户要求继续核查、修复并续跑。原100轮已完成32轮，上一续批实际4轮业务完成（界面只计3轮），新续批应为64轮，不重复第4轮提交和住宿。

## 根因与修改

1. foreground_lost_pixels来自NativeFlowPorts已捕获的非游戏原始帧；但RunStore的DiagnosticPixels入口仅允许metadata_invalid_pixels，故有效截图被拒为DIAGNOSTIC_PIXELS_INVALID。现按来源node/reason/stage严格接受前台丢失像素，保留原始尺寸和无输入授权；metadata_valid标明两类像素的区别。
2. 读取恢复已经重启或切回游戏后，旧exception_since仍包含74秒模拟器启动等待。现仅在生命周期实际恢复且已取得新游戏帧后清除旧计时；纯只读故障重试不刷新计时，总期限及真正持续60秒异常的重启规则保持有效。
3. 成功业务与图片诊断完整性混在secondary_errors空值门禁中。现已记录的纯图片失败/配额不足单独记DIAGNOSTIC_IMAGES_INCOMPLETE，告警及diagnostics.complete=false保留，业务满足终态/清理/回执条件时继续计数和下一轮。事件历史、动作回执、日志、终态或未记录诊断故障仍阻断，不能通过图片告警掩盖。
4. DeviceSession诊断记录新增utc_ms，供实际恢复动作与观察等待关联。业务crashes字段仍不作为重启次数的验收证据。

## 定向验证

- test_native_flow --restored-context-timer：前台恢复和应用重启超过异常窗口后均不继承旧计时，不额外force-stop。
- test_native_coordinator --capture-context：实际NativeFlowPorts产生横屏非游戏像素，经RunStore保存并解码为原尺寸；错误来源仍拒绝；游戏ROI和输入边界保持。
- test_native_flow --critical-recovery：未知送达/应用重启的受保护输入仍保留，实例退出中断回执仍不重放。
- test_native_application --closure-application：隔离临时目录、真实协调器/正式续轮监视入口；已通过图片失败告警保留成功轮次，磁盘空间、动作日志、终态及未记录诊断故障仍阻断后续。隔离根next/.local/c133r/app-891E3D9C，验证保留原有sentinel；详细结果在context133-repeat-verified.log。以上各项均通过，产品部署与实机续跑记录在完成后补充。

MuMu图形驱动nvoglv64.dll崩溃是已确认的外部故障。本轮修复自动恢复后的重复关闭和诊断误停，没有据此宣称显卡驱动崩溃已消除，也未修改驱动、渲染设置或用户VPN配置。

## 部署与现场

candidate133最终产品构建、冻结打包与管理器部署完成，PID39060/服务366B2248-29E7-4A0A-81A7-7B093852B2EE，旧PID45260正常退出。原17654/data，profile SHA256仍为3E873D08FB9135F558C3E3348B3BFFFFF3B73555C63A9176CC75CFF274CCB87C。

正式入口续跑64轮，请求326b640d-a218-4aec-935f-2f6d80b16d7b，协调器50CEB64B-BD6C-47E4-AEB8-19EBC0697E49。实例2/繁中/现有悬赏巨人方案，EnsureVpn及StartApplication观察到已就绪并确认，因此未重复启动。第1轮Running、repeat.active=true、target64；真实步骤已推进到Task_BountyBoard_LeaveBoard_ListBack，尚不计作整轮完成。日志仍debug及memory/performance/recognition，启动证据next/.local/giant133-64-20261009-326b640d，运行日志位于原data/runs/50CEB64B-BD6C-47E4-AEB8-19EBC0697E49。

本轮未请求commit/push，源码和报告保留于工作树；.vscode未改动。
