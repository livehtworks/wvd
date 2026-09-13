# Maa C ABI 适配层（M2）

M1 不链接或加载 Maa；锁定版本见 `../../dependencies.lock.json`。
后续只封装句柄所有权、回调、TaskDetail/RecoDetail 与停止请求，不承载游戏业务。
有效子任务 ID 不表示成功；OCR 与模板都必须返回 Hit / NoHit / Error。
不能把探针的共享状态直接复制为正式生命周期。
