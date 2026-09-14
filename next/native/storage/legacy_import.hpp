#pragma once
#include "games/wvd/profile.hpp"
#include <filesystem>

namespace wvd::storage {
nlohmann::json parse_legacy_json(const std::string &text);
class LegacyConfigImporter {
  public:
    explicit LegacyConfigImporter(nlohmann::json descriptor);
    games::WvdProfile parse(const nlohmann::json &source) const;
    games::WvdProfile import_copy(const std::filesystem::path &source,
                                  const std::filesystem::path &private_directory) const;
    nlohmann::json export_legacy(const games::WvdProfile &profile) const;

  private:
    const nlohmann::json descriptor_;
};
// 按原顺序处理单个显式 mod 文档。无效条目保留诊断，合法冲突反复追加旧后缀。
nlohmann::json merge_legacy_quests(nlohmann::json baseline, const nlohmann::ordered_json &mod,
                                   nlohmann::json &diagnostics);
} // namespace wvd::storage
