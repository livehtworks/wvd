# 2026-10-28 工具输出路径整改

用户要求检查其他工具的缓存、临时文件与构建产物路径，实际修复并验证后推送个人远端；排除`.vscode`，不动正式服务、不启动循环或线上CI。

## 实际修改

`next/tools/tool-paths.json`是当前构建/诊断子进程的路径配置。Python构建入口和PowerShell诊断入口共同读取它，后者经原有Windows Job入口传递显式Unicode子进程环境，不修改父进程或用户/系统环境变量。原有进程所有权、取消、输出上限及退出确认保持。

| 产物 | 当前有效位置/入口 |
| --- | --- |
| TEMP、TMP、TMPDIR | `next/.local/tmp/` |
| npm、.NET CLI、NuGet包/HTTP/插件、Python字节码、SymCache | `next/.local/cache/`各自子目录 |
| 固定头文件依赖 | 默认`next/.local/cache/headers/`；原生依赖仍为已有`.local/native-deps/` |
| .NET工具输出/中间文件 | `next/build/dotnet/bin/<项目>/`、`next/build/dotnet/obj/<项目>/` |
| 原生CMake、前端 | 原有`next/build/`、`next/web/dist/`及`node_modules/`，不另起产物目录 |
| CDB符号缓存 | 默认本次EvidenceRoot下`symbol-cache/`；显式参数只允许项目`.local/`或本次证据目录内，越界在任何服务请求前拒绝 |

`build.py --heap-analyzer`提供受上述路径控制的实际恢复/构建入口；原默认产品构建入口保留。收集、导出及合同入口同步使用新的分析器路径，旧bin路径没有兼容回退。`Directory.Build.props`仅作用于.csproj，避免改变C++项目的构建布局。

原`C:/Users/thevil/AppData/Local/WvdNext/dependencies`已迁入项目：32,015文件、513,716,312逻辑字节，无链接；迁移前后按锁定归档重核消费字节，身份均为`812fa3dd…f3162`，没有重新下载/更换版本。派生dependencies.json及CMake生成的头文件引用已更新。该旧缓存中2026-10-10遗留的解压暂存目录另移到D盘trash；旧工具旁4个bin/obj根（115文件、16,661,391B）在新工具验证后同盘迁入`D:/trash/wvd-tool-paths-20261028/`，目录身份/样本SHA保持，恢复清单与缓存补充清单分别保留。通用用户级npm/NuGet缓存不作本项目独占材料处理。

## 验证与限制

- 34项首轮Python定向合同通过；新增服务请求前缓存拒绝、真实junction拒绝等5项最终路径检查通过，其中3项与首轮重复，合计36项不同检查。实际CreateProcess及后代进程写临时文件、中文环境值和父环境不变均通过。
- 原19项进程/输出门禁通过，包括超时、取消、活动输出、异常收尾、后代管道及带空格batch的exit41。
- npm实际cache verify/config get cache通过；两个.NET工具在新路径实际构建，堆聚合和UTF8/128MiB进程额度自检通过；两项目的MSBuild实际输出/obj/NuGet属性逐项核对。
- 固定头文件消费身份迁移前后相同；CMake仅重新配置成功，生成项目引用新路径。没有构建或打包新的原生产品版本。
- 46份受保护文件前后SHA一致，覆盖41份正式配置、正式EXE/PDB及默认EXE/PDB；没有调用正式服务API或运行采集。C盘根SymCache未重建，原先D盘空SymCache保持不动。

额外尝试旧`test_delivery_preflight.py`时，首次以unittest调用未发现用例，随后按脚本入口执行，发现其原有夹具缺少后来新增的OCR锁/编译哈希，故失败。文件本轮未改，属历史夹具与现行校验失配；失败日志原样保留，没有跳过或伪造该脚本成功，也没有为修无关旧夹具放宽生产校验。本轮构建/目录/进程行为由上述实际入口和定向合同覆盖。

证据摘要：[可核验摘要](../evidence/tool-output-paths-20261028-summary.json)。原始小回执及日志在`next/.local/tool-path-audit-20261028/`和该摘要列出的`.local/logs/`路径。工具构建产物约15.89MiB，验证小产物不足1MiB；不保留新的完整生产快照。Actions仓库开关核对为false，仅推送个人远端，正式历史C09及T0关闭状态不因本轮工具整改改变。

## 补做遗漏：旧AppData运行数据

上一轮修复了工具缓存，却未将启动入口的默认运行数据及已遗留目录一并收束，这是检查遗漏。用户随后明确要求整树迁入trash：旧`C:/Users/thevil/AppData/Local/WvdNext`已同盘移动至`C:/trash/wvd-legacy-appdata-20261028/Users/thevil/AppData/Local/WvdNext`，65,147文件/9,759,357,869逻辑字节，无reparse点；根FileID与原profile/workflows哈希保持，原路径不存在。没有复制完整快照或永久删除，恢复清单在该trash根和项目`.local/legacy-appdata-retirement-20261028/manifest.json`。

源码管理入口取消默认AppData路径：没有显式DataRoot或有效现存绑定时失败关闭，绑定目录已归档/缺失也不能默默重建，卷根路径拒绝；原生CLI已要求显式路径，无需另改执行器。15项真实管理脚本/隔离候选合同通过，随后新增卷根拒绝用例并扩展现有有效绑定省参复用断言，分别通过（当前16项不同用例）；没有删弱原有显式路径复用断言。正式进程PID47024/创建身份/EXE仍为原D盘实例，未发正式服务请求、未重打包或改写冻结候选、未启动循环或CI。这次归档不关闭仍待处理的D盘正式历史C09。

本项的[独立补充证据](../evidence/legacy-appdata-retirement-20261028-summary.json)保留目录迁移和各次验证范围；原始小回执留在项目控制目录，已退出的三个可重建测试payload根另移入D盘trash，不与C盘历史数据混放。
