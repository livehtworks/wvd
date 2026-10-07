#pragma once
#include "native_program.hpp"
#include "recognition/request.hpp"
#include <filesystem>
#include "platform/windows/preparation_timer.hpp"

namespace wvd::games::tasks {
struct NativePublication {
    workflow::FlowProgram program;
    recognition::Bundle bundle;
    nlohmann::json identity;
    nlohmann::json preparation = nlohmann::json::object();
};

NativePublication publish_native(const CompiledWorkflow &workflow,
    const recognition::Bundle &baseline, const std::filesystem::path &destination,
    const nlohmann::json &aliases, const nlohmann::json &source_paths = nlohmann::json::object(),
    const recognition::Bundle *mod = nullptr,
    const std::function<void()> &check_cancel = {},
    const platform::PreparationObserver &observer = {});
} // namespace wvd::games::tasks
