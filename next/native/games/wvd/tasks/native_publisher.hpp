#pragma once
#include "native_program.hpp"
#include "recognition/request.hpp"
#include <filesystem>
#include "platform/windows/preparation_timer.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <optional>

namespace wvd::games::tasks {
struct NativePublication {
    NativePublication(workflow::FlowProgram value, recognition::Bundle resources,
        nlohmann::json id, nlohmann::json metrics)
        : program(std::move(value)), bundle(std::move(resources)), identity(std::move(id)),
          preparation(std::move(metrics)) {}
    NativePublication(NativePublication &&) = default;
    NativePublication &operator=(NativePublication &&) = default;
    ~NativePublication() { bundle.lease.reset(); }
    workflow::FlowProgram program;
    recognition::Bundle bundle;
    nlohmann::json identity;
    nlohmann::json preparation = nlohmann::json::object();
    std::unique_ptr<platform::OwnedBundleDirectory> ownership;
  private:
    friend struct NativePublicationTestAccess;
    friend nlohmann::json admit_existing_publication(const platform::BundleLease &,
        const std::filesystem::path &, std::uint64_t);
    friend NativePublication publish_native(const CompiledWorkflow &, const recognition::Bundle &,
        const std::filesystem::path &, const nlohmann::json &, const nlohmann::json &,
        const recognition::Bundle *, const std::function<void()> &, const platform::PreparationObserver &,
        const std::function<void(const nlohmann::json &)> &);
    static thread_local std::function<void(const char *)> fault_;
    static thread_local std::optional<std::uint64_t> available_override_;
    static void checked_add(std::uint64_t &total, std::uint64_t value);
    static std::uint64_t rounded(std::uint64_t value);
};

NativePublication publish_native(const CompiledWorkflow &workflow,
    const recognition::Bundle &baseline, const std::filesystem::path &destination,
    const nlohmann::json &aliases, const nlohmann::json &source_paths = nlohmann::json::object(),
    const recognition::Bundle *mod = nullptr,
    const std::function<void()> &check_cancel = {},
    const platform::PreparationObserver &observer = {},
    const std::function<void(const nlohmann::json &)> &admit_storage = {});
// Maintenance uses the same retained-set writer and publication validator.
// The caller owns a newly created, identity-recorded empty stage. No old tree
// is adopted and the original control files are copied as bytes.
void prepare_existing_publication(const platform::BundleLease &source,
    const std::filesystem::path &stage);
nlohmann::json admit_existing_publication(const platform::BundleLease &source,
    const std::filesystem::path &stage, std::uint64_t transaction_bytes);
} // namespace wvd::games::tasks
