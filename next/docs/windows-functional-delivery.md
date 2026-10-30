# Windows 功能交付

更新：2026-10-31。当前正式候选、暂停状态及设备身份见[项目状态](../../docs/project-status.md)，一般入口见[Next README](../README.md)。

## 启动和数据

交付入口为 `next/dist/wvd-next-native/启动WVD原生版.bat`，使用原 `manage_service.ps1`。首次 Start/Deploy 必须指定 data-root，后续仅复用有效 `service-launch.json`；没有 AppData 默认目录。当前部署使用 17654，正式 data-root 保持原 D 盘目录。文档整理不启动它。

## 用户入口

1. 工作台核对设备路径、实例和 ADB/VPN 配置；保存使用真实 revision CAS，不覆盖用户流程。
2. 在目标、探索和战斗配置选择任务；任务运行仍经 Application、Coordinator、Session 和 FlowExecutor。
3. 流程编辑使用 WorkflowRepository；调试执行已保存版本，试识别不授权设备输入。
4. 运行区展示真实状态、步骤、输入和错误；停止同时取消当前执行及原 repeat。页面关闭不等于服务停止。

## 验收范围

构建、打包、部署、真实启动、停止和业务成功分别记录。当前巨人目标 24/200，剩 176 暂停；不把旧任务计数或旧候选等待流程验收写作当前全任务通过。没有完成全任务长期挂机与全部第三方最坏停止时间的证明。

当前能力和限制见[核心合同](core-contracts.md)、[公开任务范围](public-task-scope.md)及[维护事项](../../docs/maintenance.md)。旧实操过程已进入 Git，不再复制一份完整报告。
