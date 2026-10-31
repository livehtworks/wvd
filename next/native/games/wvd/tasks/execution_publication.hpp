#pragma once
#include <filesystem>
#include <functional>
#include <string>

namespace wvd::games::tasks {
// One derived execution slot, shared by task/workflow/repeat/handoff entry
// points. requests/ and runs/ remain the history and idempotency authorities.
class ExecutionPublication {
  public:
    explicit ExecutionPublication(std::filesystem::path data_root);
    std::filesystem::path destination() const;
    void prepare(const std::string &revision) const;
    // Called by Coordinator before its worker can send any device input.
    // On failure to create the worker the returned action restores the binding.
    std::function<void()> bind(const std::filesystem::path &run,
        const std::string &request, const std::string &revision) const;
  private:
    std::filesystem::path data_, root_;
};
}
