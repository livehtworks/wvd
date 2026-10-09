# 架构审核后续修复

## 提交范围

上一轮源码、报告及证据已经提交并推送至个人fork的agent/local-stability-notes：ee024eb。原16份回执中的implementation_commit=null是当时尚未提交的事实，不覆盖历史验收记录；后续修复随本报告另行提交。用户.vscode不纳入提交。

本轮仍不部署、不修改正式profile/workflows、不启动游戏/循环或现场采集。

## 已修复的实际遗漏

1. WorkflowRepository的read_plain_file取得文件句柄后构造std::string，bad_alloc会越过手动CloseHandle，留下句柄及只读文件锁。改为取得句柄后立即由局部RAII接管。没有修改读取/校验/CAS/事务语义。
2. run_contracts的changed-from只看git diff，漏掉未跟踪新源码，可能误报没有受影响模块。现在合并NUL分隔的tracked/staged与限定产品范围内的untracked路径；用户.vscode仍排除。依赖/生成/验收入口变更触及六模块，独立工具测试只触及tools，不静默省略也不无故扩大。

## 实际验证

- [旧句柄泄漏](architecture-audit-followup-20261010/read-oom-before.log)返回exit1、READ_ALLOCATION_LEAKED_HANDLE；[修复后](architecture-audit-followup-20261010/read-oom-after.log)exit0。故障由测试EXE的定向operator new触发，先以真实Windows共享冲突确认文件已经打开再抛bad_alloc；未在生产代码加测试开关。进程句柄数稳定、文件重新可打开、原文档正常重读。
- 实际临时Git仓库检查新增未跟踪/已暂存文件及依赖模块选择，六个资源/构建合同检查通过，见[module-selection.log](architecture-audit-followup-20261010/module-selection.log)。其中超时输出是预期负样本，不是被吞掉的失败。
- 实际RunStore保存两张不同PNG，两个实例都使用run=1、generation=1、id=1；新图保存后释放延迟旧读取，旧图字节不被新图替代。错误实例/代次明确NotFound；错误目录搭配旧索引明确HashMismatch。这覆盖存储层，未冒充HTTP/Vue完整端到端。
- 全部受影响模块通过，源及产物身份核对一致：[回执](architecture-audit-followup-20261010/receipt.json)、[执行输出](architecture-audit-followup-20261010/followup-contracts-1.log)。源码输入hash为60b68ff66c1b5f7733280e6f81cefec56c8f6b9d59dbd148b03906fc64c047e7；41次原生命令及11项工具/构建不是游戏轮数。

实际命令：

```powershell
cmake --build next/build --config Release --target test_native_application
# 在next目录分别用修复前/后产物与两个全新隔离根执行
./build/native/Release/test_native_application.exe --repository-read-oom .local/architecture-audit-20261010/read-oom-before
./build/native/Release/test_native_application.exe --repository-read-oom .local/architecture-audit-20261010/read-oom-after
C:/Python314/python.exe -m unittest discover -s next/tests -p test_build_resource_contract.py
C:/Python314/python.exe next/tools/run_contracts.py --changed-from ee024eb --output next/.local/architecture-audit-20261010/followup-contracts-1
git -c core.safecrlf=false diff --check
```

修复前产物对应ee024eb生产读取函数加同一个正向故障断言；修复后只有读取函数增加即时RAII。已存在的隔离根不可复用，重新核验须使用新目录。

## 仍需证据的项

- RESOURCE_UNRESOLVED：此次句柄缺陷是可复现代码漏洞，不据此认定它就是此前PrivateUsage增长来源。仍缺真实同阶段分配栈/owner/释放位置及符号、丢事件、VA覆盖。
- libRenderer自然崩溃仍缺dump/fault栈与厂商SDK契约；取证计划仍UNARMED。
- 专属疗效、面具技能面板和部分随机事件真实素材仍未补齐。没有猜图、放宽ROI或把返回页面当疗效。
- WP03逐SDK/真实forward故障、WP07真实ETL投影/地址复用、WP08/09繁中完整链/复杂多框成本、WP11 HTTP/Vue并发换轮、WP13完整批次边界、WP14全部最小builtin、WP15全部旧正向反例及远端CI仍未完整验收。存储侧图片交错与未跟踪源码漏验已在本轮补上；其余不能批量宣布关闭。

当前总清单仍以[逐包报告](architecture-audit-remediation-20261010.md)为历史基线，本报告补充新的修复及收窄后的缺口，不改写原证据。
