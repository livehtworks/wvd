# M3 资源锁与停止生命周期补验

本项补齐指定资源矩阵，不改变资源协议，也不重跑固定性能窗口、M0或已经达到修正上限的
MetadataQuery测试。全部文件操作仅限本例新建的资源副本和独立run-data。

## 验收断言

- 已有模板/OCR入口测试增加Pipeline、alias、参数文件保护；每个文件分别尝试截断、删除、
  改名和外部文件替换，全部拒绝且SHA256不变。改作者副本后，实际SDK和Custom Pipeline
  仍使用活动副本；释放后写入成功，新加载原manifest拒绝。
- 同一个作者包同时持有两个只读lease，读者可共存，不把额外句柄误当写入冲突。
- 十三种非法路径（穿越、绝对路径、ADS、反斜线、NUL、设备名、尾点/空格、通配符及空值）
  在初始化前拒绝，既有锁全部释放，合法模板不变，输入为零。
- 复用M2的stop-timeout与release-timeout：阻塞底层输入和触点释放失败分别导致
  STOP_TIMEOUT；此时全部活动资源仍拒绝写句柄。只有原生调用/释放真实完成，SDK对象销毁且
  quiescent后才能重新取得写句柄。不中断SDK线程、不放宽停止期限、不强杀测试当成功。

## 证据状态

`4420fcf` 构建成功。完整性六方法五通过、一失败（16.670秒，`m3-fixes-55vpf4_f`，
日志 `m3-integrity-lifetime.log`）：十三种非法路径中 `/absolute.png` 被漏检，实际到了
打开文件步骤才报INTEGRITY_SHARING_CONFLICT，而不是预期的RESOURCE_PATH_INVALID。
没有向根目录写入测试文件。其它路径、全成员操作保护、双只读者、延后SDK/Custom识别通过。

根因：Windows的带根目录但无盘符路径不满足is_absolute且没有root_name，却不允许作为
包内相对路径拼接。修正一使用has_root_path同时拒绝root_directory/root_name；不改manifest
或降低测试断言。待复验，不把错误类型不正确的负例算PASS。

同次修正审查另外两处预检：裸模板名称在拼接`image/`之前，以及无lease的文件预检，
统一调用BundleLease::checked_relative，不再保留较弱的重复路径判断。增加SDK模板请求的
无盘符根路径和ADS两个独立负例；是在首次修正复验前补齐调用链，不是复测失败后改断言。

同构建M2的三个timeout方法通过（2.896秒，`m3-integrity-timeout-lifetime.log`）。
新增资源锁断言分别随stop-timeout、release-timeout执行；原生输入阻塞和触点释放失败时
全部快照成员都无法取得写句柄，真实静止后均可打开。不代表任意原生等待可被及时取消。
资源成本与发现链的既有未决项独立保留。
