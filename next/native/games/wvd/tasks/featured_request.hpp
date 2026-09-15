#pragma once
#include "pipeline_compiler.hpp"

namespace wvd::games::tasks {
enum class FeaturedRequest { BullCave, GoldenChest, Fordraig };
// 包含真实住宿、菜单定位和返回城内；返回成功不等于已验证奖励或任务完成。
CompiledWorkflow accept_featured_request(FeaturedRequest request, bool royal_suite);
}
