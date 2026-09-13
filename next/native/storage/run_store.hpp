#pragma once
#include "contracts/run.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <json.hpp>
#include <mutex>

namespace wvd::storage {
class EventJournal {
  public:
    EventJournal(std::string instance, std::uint64_t run, std::size_t capacity = 256);
    std::uint64_t emit(std::uint64_t generation, std::string type, nlohmann::json payload = {},
                       bool critical = false);
    nlohmann::json read(std::uint64_t after = 0) const;
    void commit_terminal(std::uint64_t generation, nlohmann::json payload,
                         const std::function<void(const nlohmann::json &)> &persist);

  private:
    struct Event {
        std::uint64_t seq;
        bool critical;
        nlohmann::json value;
    };
    std::string instance_;
    std::uint64_t run_{}, sequence_{}, dropped_through_{};
    std::size_t capacity_{};
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    bool terminal_{};
    bool committing_{};
};
class RunStore {
  public:
    RunStore(const std::filesystem::path &root, const std::string &instance, std::uint64_t run,
             const nlohmann::json &frozen_definition);
    void save_events(const EventJournal &events);
    void save_terminal(const contracts::RunSnapshot &snapshot,
                       const contracts::SessionResult &session,
                       const nlohmann::json &events = nlohmann::json::object());
    const std::filesystem::path &directory() const {
        return directory_;
    }
    static nlohmann::json read_summary(const std::filesystem::path &directory);

  private:
    std::filesystem::path directory_;
    bool saved_{};
};
nlohmann::json snapshot_json(const contracts::RunSnapshot &snapshot);
} // namespace wvd::storage
