#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace wvd::authoring {
struct ValidatedGraph {
    std::map<std::string, const nlohmann::json *> nodes;
    std::map<std::string, std::vector<const nlohmann::json *>> success;
    std::map<std::string, std::vector<const nlohmann::json *>> failure;
    std::string success_end;
};

// 纯文档契约，不接触设备、游戏状态或编译输出。
ValidatedGraph validate_graph(const nlohmann::json &document);
void validate_author_workflow(const nlohmann::json &document);
} // namespace wvd::authoring
