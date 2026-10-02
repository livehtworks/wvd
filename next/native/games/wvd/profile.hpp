#pragma once
#include <json.hpp>

namespace wvd::games {
// 值对象保存导入时的完整语义树。运行定义持有副本，不反复读取可编辑配置。
struct WvdProfile {
    nlohmann::json values;
    nlohmann::json legacy_document;
    nlohmann::json legacy_passthrough;
    nlohmann::json sources;
    std::string selected_section;
};
void validate_strategy(const nlohmann::json &strategy);
// 仅用于旧配置导入/一次性存储迁移；执行器只消费两种规范频次。
void normalize_strategy(nlohmann::json &strategy);
} // namespace wvd::games
