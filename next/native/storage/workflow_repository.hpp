#pragma once

#include <filesystem>
#include <json.hpp>
#include <string>

namespace wvd::storage {

// 每个流程独占 <flow-id>.json。调用方不能传相对路径或文件名。
class WorkflowRepository {
  public:
    explicit WorkflowRepository(std::filesystem::path root);

    nlohmann::json create(const nlohmann::json &document);
    nlohmann::json list() const;
    nlohmann::json read(const std::string &flow_id) const;
    // 一次锁定整个实际引用闭包；返回纯值快照，不在运行中回读库文件。
    nlohmann::json snapshot_closure(const nlohmann::json &root) const;
    nlohmann::json copy(const std::string &source_id, const std::string &new_id,
                        const std::string &new_name);
    nlohmann::json compare_exchange(const std::string &flow_id,
                                    const std::string &expected_revision,
                                    const nlohmann::json &document);
    void erase(const std::string &flow_id, const std::string &expected_revision);
    void register_builtin(const nlohmann::json &document, const std::string &local_revision);
    nlohmann::json inspect_builtin(const nlohmann::json &document) const;
    nlohmann::json sync_builtin(const nlohmann::json &document,
                                const std::string &expected_local_revision,
                                const std::string &expected_builtin_revision);

  private:
    const std::filesystem::path root_;
    std::filesystem::path path_for(const std::string &flow_id) const;
    std::filesystem::path builtin_path_for(const std::string &flow_id) const;
    nlohmann::json read_unlocked(const std::string &flow_id) const;
};

} // namespace wvd::storage
