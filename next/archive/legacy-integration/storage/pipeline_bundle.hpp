#pragma once
#include "maafw/recognition.hpp"

namespace wvd::storage {
// 必须在冻结RunDefinition/权限中的pack_revision之前显式调用。
// 只发布到新的隔离目录，不改作者包，也不在Gateway中隐式改写已授权的图。
maafw::Bundle prepare_pipeline_bundle(const maafw::Bundle &source,
                                      const std::filesystem::path &destination);
}
