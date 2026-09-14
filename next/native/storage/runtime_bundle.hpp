#pragma once
#include "maafw/recognition.hpp"

namespace wvd::storage {
// 作者资源与活动资源是两种身份。只物化副本；不会清理、覆盖作者目录。
maafw::Bundle materialize_bundle(const maafw::Bundle &source);
void validate_bundle_references(const maafw::Bundle &bundle, const nlohmann::json &pipeline);
} // namespace wvd::storage
