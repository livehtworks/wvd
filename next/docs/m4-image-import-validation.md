# M4 图片扩展导入

## 唯一选择规则

`vision::resolve_image_source` 被实际识别、流程发布及静态编译资源核对共用：先查基线原名，再查基线显式 alias，缺失后查 mod 原名及显式 alias。alias 不递归展开，不自动猜测名称大小写。已经找到但内容损坏的基线资源明确报错，不静默用 mod 掩盖资源错误。

此前 AssetResolver 和发布器都会先改 alias 再查基线，可能跳过已存在的原名。此次同时修改两个真实调用点和静态核对，不保留另一套优先级。

## 导入与隔离

`publish_workflow` 接受调用者显式提供的只读 mod Bundle，只允许 image 成员。原图、alias 与 mod 选择完毕后，持有两侧 BundleLease 复制到不存在的新目的目录，所用扩展图加入同一个发布包。没有新的运行后备执行器；游戏识别仍然只读当前 Session 的活动快照。

包身份包含基线与 mod revision/manifest、最终图片选择及注册表身份。`parameters/image-sources.json` 保存无私人绝对路径的来源记录并纳入 hash 清单。缺失图片、变更 hash、非法成员等在连接前拒绝；新包不能覆盖已有目的目录。

## 验证状态

五项真实 Maa 离线断言全部通过：基线原名胜过 alias/mod、基线 alias 胜过 mod 原名、mod 发布后源变更不影响运行、坏基线不能被好 mod 遮掩、发布前 mod hash 变化必须零连接拒绝。它们属于 `m4-effect-image-targeted.log` 的 11 方法组（215.150 秒），私有逐项证据在 `next/.local/m4-workflow-hwe1dv2i/`。旧 M3 的来源/别名、同名跨包缓存及原生识别测试仍须回归。

同产物回归：M2 全部 102 方法通过（94.109 秒）；M3 ROI/受控动作/组合条件 3 方法与来源/别名、Custom、完整资源负例、Pause 布局、颜色/视觉族、浮标对照 6 方法共 9 方法通过（51.340 秒）；静态计划 6 方法通过（17.742 秒）。日志分别为 `m4-boundaries-m2.log`、`m4-boundaries-m3.log`、`m4-boundaries-plan.log`。没有重跑已到有限修正边界的发现/清理测试。

原 config/mod/resources 不变。真实用户 mod 导入仍未验证，不把合成图片成功称为真实任务或图片质量通过。
