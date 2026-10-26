#pragma once
#include <filesystem>
#include <functional>
#include "json.hpp"

namespace wvd::app {
class PublicationMaintenance final {
  public:
    using Progress = std::function<void(const nlohmann::json &)>;
    // Every maintenance command acquires the original ServiceInstance lock.
    // plan/convert take a request ID; status/finish/rollback a transaction ID.
    static nlohmann::json execute(const std::filesystem::path &data_root,
        const std::string &command, const std::string &id, const Progress &progress = {});
    static void require_no_pending(const std::filesystem::path &data_root);
};
}
