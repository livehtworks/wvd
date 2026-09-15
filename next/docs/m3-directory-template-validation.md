# M3 目录模板封存

## 生产路径

`storage::prepare_pipeline_bundle` 在冻结RunDefinition及InputPolicy的pack_revision前显式调用。
M4离线检查CLI的 `prepare_pipeline_bundle` 请求可消费它；只准备派生包，不执行任务或连接设备。
编译器生成的WVD图继续走既有publish_workflow，不新增第二个运行器或运行时改写入口。

源manifest经BundleLease持锁后展开目录模板，保持实际递归枚举顺序与重复项，将具体文件数组
写进派生Pipeline。阈值数组不按目录重复，因为固定SDK按最终图片序号取阈值，超出后取末值。
来源清单、目录展开结果、输出文件hash进入派生身份；原作者文件不修改、已存在目的目录拒绝。
运行阶段仍执行原有活动封存/成员检查，禁止SDK再延迟枚举目录或override引入新目录。

固定源码依据：[TemplateResMgr](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaFramework/Resource/TemplateResMgr.cpp)、
[TemplateMatcher](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaFramework/Vision/TemplateMatcher.cpp)。
目录中的损坏图像在准备时明确拒绝，不沿用SDK忽略损坏文件的宽松行为；此处不放宽完整性。

## 待执行验收

- 嵌套目录及混合文件数组保留枚举顺序、阈值和重复项，派生图在真实固定Maa中到达根终点。
- 准备后修改作者图/新增成员不影响旧派生包；旧manifest再次准备必须失败。
- 空目录、损坏图、越界路径在运行前失败，零输入；已存在目的目录不覆盖。
- CLI准备入口、派生资源身份与M2必要回归。

当前实现与测试已写，尚待新构建。既有M3发现/清理与资源性能阻断保持，不重跑其有限窗口。

首轮构建 `524e52d` 在SetEncoded的const指针处失败，未运行测试；沿用既有preflight做法，
只复制单张编码图给固定C ABI，不使用const_cast暴露源lease。修正后再构建和执行。

`4972ff6` 构建后，目录模板两方法/五场景通过（2.981秒，`m3-fixes-1ly984rz`），
包含两场真正Maa Pipeline执行；CLI准备一方法通过（0.486秒）。运行时DLL路径/hash由
既有夹具逐项核对固定SDK，零底层输入。随后补目的目录不得嵌入源包的身份检查与CLI负例，待新构建。

`4b408ea`构建再验：目录/窗口三方法通过，M4数据全组通过，含目的目录嵌入源包时拒绝且
不创建子目录。M2同构建102方法通过；没有重跑M3已阻断的发现/清理组和固定成本窗口。
