# 战斗方案复制与排序

## 交付范围

- 方案右上提供复制、上移、下移；复制全部角色配置和整组完成开关，自动生成不重复的副本名称，插在原方案后并选中。
- 每条角色配置提供复制、上移、下移；复制后插在原行下方，字段独立，调整等级等不会回写原行。
- 首尾移动禁用；排序跟随真实数组而不是搜索结果，移动方案后清空搜索，当前选中方案保持。
- 复用原配置保存及CAS链，不修改方案名称引用、不改后端执行规则。行顺序会影响同角色的动作选择；运行中的任务仍使用既有冻结配置。
- 未加入跨方案转移、拖拽或批量操作；本轮移动指上下排序。

## 验证

扩展既有`next/web/tests/strategy-frequency.spec.ts`，在独立目录
`next/.local/strategy-order-1003/data`的正式profile只读副本上运行真实原生服务，端口18758。
仅允许GET与profile PUT，未连接设备或触发游戏任务。

桌面1440×1000和窄屏390×844均通过：边界禁用、深复制、副本名称冲突、行上下移动、搜索下方案上下移动、服务端保存与重载。
同时核对全部非STRATEGY字段、已有其它方案不变；六个友方位置及两种频次原检查保留。
日志：`next/.local/strategy-order-test.log`；截图在`next/web/test-results`对应项目目录。
前端类型检查与构建通过，截图已人工核对。

## 部署

- candidate72已由受管Deploy部署到17654，旧PID29492正常退出，新PID44984，实例`62B7A28B-3A1B-4920-B5D9-6786633E19E6`；原数据根保持。当前Idle、run_id=0。
- 页面实际返回新资源`index-CVSAyd49.js`与`index-D7n8a2Wo.css`。构建/打包日志为`next/.local/strategy-order-{native,web-final,package}.log`，部署日志为`next/.local/strategy-order-deploy.log`。
- 正式profile部署前后SHA256均为`1A5A5C4709644A3166F5C38F603C8BD44F4A49E8D54AC199E935EF94EF4338CB`。测试服务18758已退出；没有操作游戏或启动循环。
- 如需回滚，由受管Deploy切回candidate71并保持原data根，本轮未新增配置格式或迁移。部署后仅补记本文和滚动事实，不改变已打包源码。未执行commit/push。
