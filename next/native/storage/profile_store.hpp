#pragma once
#include "games/wvd/profile.hpp"
#include "legacy_import.hpp"
#include <filesystem>

namespace wvd::storage {
class ProfileStore {
  public:
    // 调用者必须指定新版隔离路径；绝不推导或写入旧 config.json。
    ProfileStore(std::filesystem::path path, nlohmann::json descriptor);
    nlohmann::json create(const games::WvdProfile &profile);
    nlohmann::json load() const;
    nlohmann::json compare_exchange(const std::string &revision, const nlohmann::json &document);

  private:
    const std::filesystem::path path_;
    const LegacyConfigImporter importer_;
};
} // namespace wvd::storage
