#pragma once
#include <json.hpp>
#include <string>
#include <vector>

namespace wvd::games {
struct WvdQuestDefinition {
    std::string id, type;
    nlohmann::json source;
};
// 目录绑定不是计划编译，更不是任务完成。原树保留全部路线/回调字段供后续编译消费。
class WvdQuestCatalog {
  public:
    explicit WvdQuestCatalog(const nlohmann::ordered_json &source);
    const std::vector<WvdQuestDefinition> &tasks() const { return tasks_; }
    const WvdQuestDefinition &at(const std::string &id) const;
    nlohmann::json export_source() const;

  private:
    std::vector<WvdQuestDefinition> tasks_;
};
} // namespace wvd::games
