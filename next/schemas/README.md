# 数据契约边界

当前只提供 API v1 的版本及能力查询，定义在 `native/contracts/version.hpp`，
前端消费类型在 `web/src/api/types.ts`，真实响应由 `tests/test_service.py` 核对。
盘点 JSON 的 `schema_version=1` 是只读报告版本，不是用户配置版本。
M2 起再按已确认接口建立配置、manifest、编辑元数据 schema；M1 不放占位任务或假配置。
