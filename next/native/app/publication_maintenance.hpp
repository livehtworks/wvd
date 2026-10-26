#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include "json.hpp"

namespace wvd::app {
class PublicationMaintenance final {
  public:
    using Progress = std::function<void(const nlohmann::json &)>;
    struct RepeatEvidence { std::string root_request_id; std::uint64_t cycle{}; };
    // Every maintenance command acquires the original ServiceInstance lock.
    // plan/convert take a request ID; status/finish/rollback a transaction ID.
    static nlohmann::json execute(const std::filesystem::path &data_root,
        const std::string &command, const std::string &id, const Progress &progress = {},
        const std::optional<RepeatEvidence> &repeat = {});
    static void require_no_pending(const std::filesystem::path &data_root);
};
}
