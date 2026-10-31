#include "execution_publication.hpp"
#include "handoff_provenance.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/path_utf8.hpp"
#include <windows.h>
#include <json.hpp>
#include <set>

namespace wvd::games::tasks {
namespace {
using J = nlohmann::json;
using Owner = platform::OwnedBundleDirectory;
void need(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
J read(const std::filesystem::path &path, std::uint64_t limit = 65536) {
    // Reject links before reading; pin every ancestor against path substitution.
    std::vector<std::unique_ptr<Handle>> parents;
    for (auto p = path.parent_path(); !p.empty();) {
        auto held = std::make_unique<Handle>();
        held->value = CreateFileW(platform::extended_path(p).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        need(held->value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(held->value, &info) &&
            !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "EXECUTION_PUBLICATION_PATH_INVALID");
        parents.push_back(std::move(held));
        const auto next = p.parent_path();
        if (next == p) break;
        p = next;
    }
    Handle file;
    file.value = CreateFileW(platform::extended_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    need(file.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file.value, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        GetFileType(file.value) == FILE_TYPE_DISK, "EXECUTION_PUBLICATION_RECORD_UNREADABLE");
    const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    need(size <= limit, "EXECUTION_PUBLICATION_RECORD_LIMIT");
    std::string text(static_cast<std::size_t>(size), '\0');
    DWORD got{};
    need(ReadFile(file.value, text.data(), static_cast<DWORD>(size), &got, nullptr) && got == size,
        "EXECUTION_PUBLICATION_RECORD_UNREADABLE");
    return J::parse(text);
}
void save(const std::filesystem::path &root, const J &state) {
    platform::atomic_write(root / "state.json", state.dump(), true);
}
J state_at(const std::filesystem::path &root) {
    const auto state = read(root / "state.json");
    need(state.at("schema") == 1 && state.at("kind") == "derived_execution_slot" &&
        state.size() == 5, "EXECUTION_PUBLICATION_STATE_INVALID");
    const auto phase = state.at("phase").get<std::string>();
    need(phase == "empty" || phase == "ready" || phase == "retiring", "EXECUTION_PUBLICATION_STATE_INVALID");
    need(phase != "empty" || (state.at("directory_identity").is_null() && state.at("use").is_null()),
        "EXECUTION_PUBLICATION_STATE_INVALID");
    if (!state.at("use").is_null())
        need(state.at("use").is_object() && state.at("use").size() == 3 &&
            state.at("use").at("run").is_string() && state.at("use").at("request_id").is_string() &&
            state.at("use").at("revision").is_string(), "EXECUTION_PUBLICATION_STATE_INVALID");
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().native();
        need(name == L"state.json" || name == L"current", "EXECUTION_PUBLICATION_UNKNOWN_ENTRY");
    }
    return state;
}
void verify_slot_identity(const std::filesystem::path &slot, const J &state) {
    Handle held;
    held.value = CreateFileW(platform::extended_path(slot).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    need(held.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(held.value, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY),
        "EXECUTION_PUBLICATION_PATH_INVALID");
    const Owner::DirectoryIdentity actual{info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
    need(actual == state.at("directory_identity").get<Owner::DirectoryIdentity>(), "EXECUTION_PUBLICATION_IDENTITY_CHANGED");
}
std::filesystem::path run_path(const std::filesystem::path &data, const J &use) {
    const auto relative = platform::BundleLease::checked_relative(use.at("run").get<std::string>());
    auto it = relative.begin();
    need(it != relative.end() && *it++ == "runs" && it != relative.end(), "EXECUTION_PUBLICATION_RUN_INVALID");
    ++it;
    need(it != relative.end(), "EXECUTION_PUBLICATION_RUN_INVALID");
    ++it;
    need(it == relative.end(), "EXECUTION_PUBLICATION_RUN_INVALID");
    return data / relative;
}
void require_settled(const std::filesystem::path &data, const J &use) {
    if (use.is_null()) return; // never handed to Coordinator
    const auto run = run_path(data, use);
    const auto definition = read(run / "run.json", 16ULL * 1024 * 1024);
    const auto result = read(run / "result.json", 16ULL * 1024 * 1024);
    need(definition.at("schema") == 1 && definition.at("definition").at("request_id") == use.at("request_id") &&
        definition.at("definition").at("program_revision") == use.at("revision") &&
        definition.at("run_id") == result.at("run_id") &&
        definition.at("instance") == platform::utf8(run.parent_path().filename()) &&
        std::to_string(definition.at("run_id").get<std::uint64_t>()) == platform::utf8(run.filename()),
        "EXECUTION_PUBLICATION_RUN_MISMATCH");
    const auto state = result.at("state").get<std::string>();
    need((state == "Completed" || state == "Failed" || state == "UserStopped") &&
        result.at("quiescent") == true && result.at("result_saved") == true &&
        result.at("details_complete") == true && result.at("storage_error") == "" &&
        result.value("unresolved_inputs", J::array()).empty() &&
        !handoff_has_unconfirmed_effect(result.at("business")), "EXECUTION_PUBLICATION_UNSETTLED");
}
void check_slot(const std::filesystem::path &slot) {
    Owner::require_no_pending(slot);
    for (const auto &entry : std::filesystem::directory_iterator(slot)) {
        const auto name = entry.path().filename().native();
        need(name == L"bundle" || name == L".asset-sets", "EXECUTION_PUBLICATION_UNKNOWN_ENTRY");
    }
    // Both consumers validate every member; no unknown source tree is adopted.
    if (std::filesystem::exists(slot / "bundle"))
        (void)platform::BundleLease::open_publication(slot / "bundle");
    (void)platform::BundleLease::retained_resource_bytes(slot);
}
}
ExecutionPublication::ExecutionPublication(std::filesystem::path data_root)
    : data_(std::filesystem::absolute(data_root).lexically_normal()), root_(data_ / "execution-publication") {}
std::filesystem::path ExecutionPublication::destination() const { return root_ / "current" / "bundle"; }
void ExecutionPublication::prepare(const std::string &revision) const {
    need(revision.size() == 64 && revision.find_first_not_of("0123456789abcdef") == std::string::npos,
        "EXECUTION_PUBLICATION_REVISION_INVALID");
    if (!std::filesystem::exists(root_)) {
        auto owner = Owner::create(root_);
        save(root_, {{"schema", 1}, {"kind", "derived_execution_slot"}, {"phase", "empty"},
            {"directory_identity", nullptr}, {"use", nullptr}});
        owner->accept();
        owner->receipt()->require_clean_acceptance();
    }
    auto state = state_at(root_);
    const auto slot = root_ / "current";
    if (state.at("phase") == "ready") {
        verify_slot_identity(slot, state);
        require_settled(data_, state.at("use"));
        check_slot(slot);
        if (std::filesystem::exists(destination())) {
            auto bundle = platform::BundleLease::open_publication(destination());
            if (bundle->revision() == revision) return;
        }
        // Commit intent BEFORE deleting. A crash/partial delete resumes only
        // this proven derived slot, never a historical published/ directory.
        state["phase"] = "retiring";
        save(root_, state);
    }
    if (state.at("phase") == "retiring") {
        const auto identity = state.at("directory_identity").get<Owner::DirectoryIdentity>();
        if (std::filesystem::exists(slot)) Owner::erase_retired(slot, identity);
        state["phase"] = "empty";
        state["directory_identity"] = nullptr;
        state["use"] = nullptr;
        save(root_, state);
    }
    need(state.at("phase") == "empty" && !std::filesystem::exists(slot), "EXECUTION_PUBLICATION_STATE_INVALID");
    auto owner = Owner::create(slot);
    state["phase"] = "ready";
    state["directory_identity"] = owner->directory_identity();
    save(root_, state);
    owner->accept();
    owner->receipt()->require_clean_acceptance();
}
std::function<void()> ExecutionPublication::bind(const std::filesystem::path &run,
    const std::string &request, const std::string &revision) const {
    auto previous = state_at(root_);
    need(previous.at("phase") == "ready", "EXECUTION_PUBLICATION_STATE_INVALID");
    verify_slot_identity(root_ / "current", previous);
    require_settled(data_, previous.at("use"));
    const auto relative = std::filesystem::absolute(run).lexically_normal().lexically_relative(data_);
    const auto portable = relative.generic_u8string();
    J use{{"run", std::string(portable.begin(), portable.end())}, {"request_id", request}, {"revision", revision}};
    const auto definition = read(run_path(data_, use) / "run.json", 16ULL * 1024 * 1024);
    need(definition.at("definition").at("request_id") == request &&
        definition.at("definition").at("program_revision") == revision,
        "EXECUTION_PUBLICATION_RUN_MISMATCH");
    auto lease = platform::BundleLease::open_publication(destination());
    need(lease->revision() == revision, "EXECUTION_PUBLICATION_RUN_MISMATCH");
    // Allocate rollback before the write so bad_alloc cannot orphan a binding.
    std::function<void()> rollback = [root = root_, previous] { save(root, previous); };
    auto state = previous;
    state["use"] = std::move(use);
    save(root_, state);
    return rollback;
}
}
