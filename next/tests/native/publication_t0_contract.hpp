#pragma once
#include "games/wvd/tasks/native_publisher.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/path_utf8.hpp"
#include <windows.h>
#include <winioctl.h>
#include <fstream>
#include <limits>
#include <cstring>

namespace wvd::platform {
struct BundleLeaseTestAccess {
    static void copy_fault(std::function<void(const char *)> value) { BundleLease::copy_fault_ = std::move(value); }
    static void io_fault(std::function<bool(const char *)> value) { BundleLease::io_failure_ = std::move(value); }
};
struct OwnedBundleDirectoryTestAccess {
    static void fault(std::function<void(const char *)> value) { OwnedBundleDirectory::fault_ = std::move(value); }
};
}
namespace wvd::games::tasks {
struct NativePublicationTestAccess {
    static void fault(std::function<void(const char *)> value) { NativePublication::fault_ = std::move(value); }
    static void available(std::optional<std::uint64_t> value) { NativePublication::available_override_ = value; }
    static void add(std::uint64_t &total, std::uint64_t value) { NativePublication::checked_add(total, value); }
    static std::uint64_t round(std::uint64_t value) { return NativePublication::rounded(value); }
};
}
namespace publication_t0_contract {
namespace fs = std::filesystem;
using J = nlohmann::json;
using namespace wvd;
inline bool exists_os(const fs::path &path) { return fs::exists(platform::extended_path(path)); }
inline std::uintmax_t size_os(const fs::path &path) { return fs::file_size(platform::extended_path(path)); }
inline bool remove_os(const fs::path &path) { return fs::remove(platform::extended_path(path)); }
inline bool mkdir_os(const fs::path &path) { return fs::create_directory(platform::extended_path(path)); }
inline void mkdirs_os(const fs::path &path) { fs::create_directories(platform::extended_path(path)); }
inline fs::directory_iterator directories_os(const fs::path &path) { return fs::directory_iterator(platform::extended_path(path)); }
inline fs::recursive_directory_iterator recursive_os(const fs::path &path) { return fs::recursive_directory_iterator(platform::extended_path(path)); }
inline void check(bool value, const char *code) { if (!value) throw std::runtime_error(code); }
inline void check(bool value, const std::string &code) { if (!value) throw std::runtime_error(code); }
template<class Call> inline void rejects(Call call, const char *prefix) {
    try { call(); } catch (const std::exception &error) {
        if (std::string(error.what()).starts_with(prefix)) return;
        throw std::runtime_error(std::string("T0_UNEXPECTED_REJECTION:expected=") + prefix + ";actual=" + error.what());
    }
    throw std::runtime_error(std::string("T0_EXPECTED_REJECTION:") + prefix);
}
struct Reset {
    ~Reset() {
        platform::BundleLeaseTestAccess::copy_fault({}); platform::BundleLeaseTestAccess::io_fault({});
        platform::OwnedBundleDirectoryTestAccess::fault({});
        games::tasks::NativePublicationTestAccess::fault({}); games::tasks::NativePublicationTestAccess::available({});
    }
};
inline void write(const fs::path &path, const std::string &bytes) {
    mkdirs_os(path.parent_path()); std::ofstream output(platform::extended_path(path), std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); output.close();
    check(bool(output), "T0_FIXTURE_WRITE_FAILED");
}
struct FileInfo {
    DWORD volume{}, links{}; std::uint64_t id{}, bytes{};
    J json() const { return {{"volume", volume}, {"id", id}, {"links", links}, {"bytes", bytes}}; }
};
inline FileInfo info(const fs::path &path) {
    const auto file = CreateFileW(platform::extended_path(path).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const auto open_error = GetLastError();
    check(file != INVALID_HANDLE_VALUE, "T0_FILE_INFO_OPEN_FAILED:path=" + platform::utf8(path) + ";winerror=" + std::to_string(open_error));
    BY_HANDLE_FILE_INFORMATION value{}; const bool ok = GetFileInformationByHandle(file, &value) != FALSE;
    CloseHandle(file); check(ok, "T0_FILE_INFO_READ_FAILED");
    return {value.dwVolumeSerialNumber, value.nNumberOfLinks,
        (std::uint64_t(value.nFileIndexHigh) << 32) | value.nFileIndexLow,
        (std::uint64_t(value.nFileSizeHigh) << 32) | value.nFileSizeLow};
}
inline bool same_file(const FileInfo &a, const FileInfo &b) { return a.volume == b.volume && a.id == b.id; }
inline bool has_staging(const fs::path &root) {
    for (const auto &entry : recursive_os(root)) {
        const auto name = entry.path().filename().native();
        if (name.find(L".staging-") != std::wstring::npos) return true;
    }
    return false;
}
inline std::vector<fs::path> pending_sidecars(const fs::path &root) {
    std::vector<fs::path> paths;
    for (const auto &entry : directories_os(root))
        if (entry.path().filename().native().starts_with(L".cleanup-pending-owned-")) paths.push_back(entry.path());
    return paths;
}
struct Fixture {
    fs::path root, source, published, object;
    std::string model = std::string(3 * 65536 + 17, 'm'), hash;
    recognition::Bundle baseline;
    games::tasks::CompiledWorkflow workflow;
    explicit Fixture(fs::path area, bool seed_object = true, std::size_t model_bytes = 3 * 65536 + 17)
        : root(std::move(area)), model(model_bytes, 'm') {
        source = root / "source"; published = root / "published";
        write(source / "models/a.onnx", model); write(source / "models/b.onnx", model);
        write(source / "definition.json", "{\"schema\":1}");
        hash = platform::file_sha256(source / "models/a.onnx");
        baseline = recognition::Bundle{source, "t0-boundary-fixture", {}};
        for (const auto &name : {"models/a.onnx", "models/b.onnx", "definition.json"})
            baseline.files.push_back({name, platform::file_sha256(source / name)});
        games::tasks::PipelineCompiler compiler("t0.boundary"); compiler.route("Entry", {"Terminal"});
        workflow = compiler.finish();
        object = published / "asset-objects" / hash / "payload.onnx";
        mkdirs_os(published);
        if (seed_object) write(object, model);
    }
    games::tasks::NativePublication publish(const std::string &name, const std::function<void()> &cancel = {}) {
        return games::tasks::publish_native(workflow, baseline, published / name, J::object(), J::object(), nullptr, cancel);
    }
    fs::path staging(const std::string &name) const {
        for (const auto &entry : directories_os(published))
            if (entry.path().filename().string().starts_with(name + ".staging-")) return entry.path();
        return {};
    }
    std::vector<fs::path> fill_links(DWORD wanted) {
        std::vector<fs::path> links; mkdirs_os(root / "padding");
        for (auto count = info(object).links; count < wanted; ++count) {
            const auto next = root / "padding" / ("link-" + std::to_string(count));
            check(CreateHardLinkW(platform::extended_path(next).c_str(), platform::extended_path(object).c_str(), nullptr),
                "T0_REAL_NTFS_LINK_PREFILL_FAILED"); links.push_back(next);
        }
        check(info(object).links == wanted, "T0_LINK_BOUNDARY_NOT_REACHED"); return links;
    }
};
inline void junction(const fs::path &link, const fs::path &target) {
    mkdir_os(link); const auto substitute = std::wstring(L"\\??\\") + target.native();
    const auto display = target.native();
    struct Mount { ULONG tag; USHORT length, reserved, substitute_offset, substitute_length, print_offset, print_length; WCHAR names[1]; };
    const auto length = offsetof(Mount, names) + (substitute.size() + display.size() + 2) * sizeof(WCHAR);
    std::vector<std::max_align_t> storage((length + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *mount = reinterpret_cast<Mount *>(storage.data()); mount->tag = IO_REPARSE_TAG_MOUNT_POINT;
    mount->length = static_cast<USHORT>(length - 8); mount->substitute_offset = 0;
    mount->substitute_length = static_cast<USHORT>(substitute.size() * 2);
    mount->print_offset = static_cast<USHORT>((substitute.size() + 1) * 2); mount->print_length = static_cast<USHORT>(display.size() * 2);
    std::memcpy(mount->names, substitute.c_str(), (substitute.size() + 1) * 2);
    std::memcpy(reinterpret_cast<char *>(mount->names) + mount->print_offset, display.c_str(), (display.size() + 1) * 2);
    const auto handle = CreateFileW(platform::extended_path(link).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "T0_JUNCTION_OPEN_FAILED"); DWORD written{};
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, mount, static_cast<DWORD>(length), nullptr, 0, &written, nullptr) != FALSE;
    CloseHandle(handle); check(ok, "T0_JUNCTION_CREATE_FAILED");
}
inline int probe_links(const fs::path &root) {
    check(root.is_absolute() && !exists_os(root), "T0_FRESH_PROBE_ROOT_REQUIRED");
    Fixture f(root); f.fill_links(1022);
    platform::BundleLease shared(f.object.parent_path(), f.hash, {{"payload.onnx", f.hash}});
    J attempts = J::array();
    for (unsigned i = 0; i < 5; ++i) {
        const auto before = info(f.object);
        const auto target = f.root / ("nt-link-" + std::to_string(i) + ".onnx");
        std::optional<std::uint32_t> status;
        try { shared.link_member("payload.onnx", target); }
        catch (const platform::BundleLinkError &error) { status = error.ntstatus(); }
        const auto after = info(f.object);
        J row{{"before", before.json()}, {"after", after.json()}, {"target_exists", exists_os(target)},
            {"ntstatus", status ? J(*status) : J(nullptr)}, {"path", platform::utf8(target)}};
        if (exists_os(target)) row["target"] = info(target).json();
        attempts.push_back(std::move(row));
        if (status) break;
    }
    write(root / "link-boundary-probe.json", J{{"source", info(f.object).json()},
        {"source_path", platform::utf8(f.object)}, {"attempts", attempts}, {"device_actions", false},
        {"scope", "Fresh disposable NTFS fixture; actual BundleLease::link_member/NtSetInformationFile, no injected status"}}.dump(2));
    std::cout << J{{"probe", platform::utf8(root)}, {"attempts", attempts}}.dump() << '\n';
    return 0;
}
inline J run(const fs::path &root) {
    check(root.is_absolute() && !exists_os(root), "T0_FRESH_ROOT_REQUIRED"); mkdirs_os(root);
    wchar_t volume[32768]{}, filesystem[64]{};
    check(GetVolumePathNameW(root.c_str(), volume, 32768) &&
        GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, filesystem, 64), "T0_VOLUME_INFO_FAILED");
    check(std::wstring(filesystem) == L"NTFS", "T0_REAL_NTFS_REQUIRED");
    J cases = J::array();
    {
        Reset reset; Fixture f(root / "real-link-limit", true, 8193); f.fill_links(1024);
        const auto original = info(f.object);
        auto first = f.publish("boundary-success");
        const auto a = first.bundle.lease->physical_member("models/a.onnx");
        const auto b = first.bundle.lease->physical_member("models/b.onnx");
        const auto old_a = info(a), old_b = info(b);
        check(info(f.object).links == 1024 && !same_file(info(a), original) && !same_file(info(b), original) &&
            platform::file_sha256(a) == f.hash && platform::file_sha256(b) == f.hash,
            "T0_SHARED_SET_DEPENDS_ON_OLD_LINK_CAPACITY");
        first.ownership->accept();
        auto second = f.publish("boundary-reuse");
        check(same_file(old_a, info(second.bundle.lease->physical_member("models/a.onnx"))) &&
            same_file(old_b, info(second.bundle.lease->physical_member("models/b.onnx"))) &&
            info(a).links == old_a.links && info(b).links == old_b.links &&
            first.preparation.at("source_model_bytes_after_copy") == 0 &&
            first.bundle.lease->storage_stats().model_bytes == 0, "T0_SHARED_REUSE_CHANGED_OBJECTS");
        second.ownership->accept();
        {
            platform::BundleLease shared(f.object.parent_path(), f.hash, {{"payload.onnx", f.hash}});
            bool exact = false;
            try { shared.link_member("payload.onnx", f.root / "capacity-rejected.onnx"); }
            catch (const platform::BundleLinkError &e) { exact = e.ntstatus() == platform::BundleLinkError::too_many_links; }
            check(exact && !exists_os(f.root / "capacity-rejected.onnx"), "T0_RAW_STATUS_NOT_PRESERVED");
        }
        bool late = false;
        rejects([&] { f.publish("later-failure", [&] {
            const auto stage = f.staging("later-failure");
            if (!stage.empty() && exists_os(stage / "program/identity.json")) { late = true; throw std::runtime_error("T0_LATE_FAILURE"); }
        }); }, "T0_LATE_FAILURE");
        check(late && info(f.object).links == 1024 && !has_staging(f.published) &&
            same_file(old_a, info(a)) && same_file(old_b, info(b)), "T0_ROLLBACK_CHANGED_HISTORY");
        cases.push_back({{"case", "P01-P03-shared-set-replaces-per-request-links-at-old-capacity"},
            {"old_object", original.json()}, {"resource_a", old_a.json()}, {"resource_b", old_b.json()}});
        for (const auto mode : {std::string("cancel"), std::string("write"), std::string("flush")}) {
            Fixture fresh(root / ("shared-initial-" + mode), false);
            bool copying = false, injected = false; int checkpoints = 0;
            platform::BundleLeaseTestAccess::copy_fault([&](const char *phase) {
                if (std::string_view(phase) == "before_copy") copying = true;
            });
            platform::BundleLeaseTestAccess::io_fault([&](const char *phase) {
                if (copying && mode == phase) { injected = true; return true; } return false;
            });
            const auto code = mode == "cancel" ? "PREPARATION_CANCELLED" : mode == "write" ? "NATIVE_BUNDLE_WRITE_FAILED" : "NATIVE_BUNDLE_FLUSH_FAILED";
            rejects([&] { fresh.publish(mode, [&] {
                if (copying && mode == "cancel" && ++checkpoints == 3) { injected = true; throw std::runtime_error("PREPARATION_CANCELLED"); }
            }); }, code);
            platform::BundleLeaseTestAccess::copy_fault({}); platform::BundleLeaseTestAccess::io_fault({});
            check(injected && !has_staging(fresh.published) && same_file(old_a, info(a)) && same_file(old_b, info(b)),
                "T0_SHARED_COPY_FAILURE_LEAKED_OR_CHANGED_HISTORY");
            cases.push_back({{"case", "P05-P13-shared-copy-" + mode}, {"injected", true}});
        }
    }
    {
        Reset reset; Fixture f(root / "initial-object-cancel", false); bool copying = false, before_copy_reached = false; int checkpoints = 0;
        platform::BundleLeaseTestAccess::copy_fault([&](const char *phase) {
            if (std::string_view(phase) != "before_copy") return;
            before_copy_reached = true;
            if (!exists_os(f.published / ".asset-sets")) return;
            for (const auto &entry : directories_os(f.published / ".asset-sets"))
                if (entry.path().filename().string().find(".staging-") != std::string::npos && exists_os(entry.path() / "payload/models/a.onnx")) copying = true;
        });
        try {
            rejects([&] { f.publish("cancel", [&] {
                if (copying && ++checkpoints == 3) throw std::runtime_error("PREPARATION_CANCELLED");
            }); }, "PREPARATION_CANCELLED");
        } catch (const std::exception &error) {
            throw std::runtime_error("T0_INITIAL_COPY_CANCEL_CHECK_FAILED:path=" + platform::utf8(f.root) +
                ";before_copy_reached=" + std::to_string(before_copy_reached) + ";copying=" + std::to_string(copying) +
                ";checkpoints=" + std::to_string(checkpoints) + ";actual=" + error.what());
        }
        check(copying && checkpoints == 3 && !exists_os(f.object) && !has_staging(f.published), "T0_INITIAL_STREAM_CANCEL_LEAKED_OBJECT_STAGE");
        cases.push_back({{"case", "P04-initial-object-cancel-inside-real-copy-blocks"}, {"copy_checkpoints", checkpoints},
            {"before_copy_reached", before_copy_reached}, {"path", platform::utf8(f.root)}});
    }
    {
        Reset reset; Fixture f(root / "collision-and-reparse");
        platform::BundleLease shared(f.object.parent_path(), f.hash, {{"payload.onnx", f.hash}});
        const auto collision = f.root / "collision.onnx"; write(collision, "external-marker");
        const auto marker_hash = platform::file_sha256(collision); bool typed_non_capacity = false;
        try { shared.link_member("payload.onnx", collision); }
        catch (const platform::BundleLinkError &e) { typed_non_capacity = e.ntstatus() != platform::BundleLinkError::too_many_links; }
        check(typed_non_capacity && platform::file_sha256(collision) == marker_hash, "T0_LINK_COLLISION_OVERWROTE_OR_LOST_ERROR");
        rejects([&] { shared.copy_member("payload.onnx", collision); }, "NATIVE_BUNDLE_CREATE_FAILED");
        const auto target = f.root / "junction-target"; mkdir_os(target);
        junction(f.root / "junction", target);
        const auto before_reparse = info(f.object);
        rejects([&] { shared.link_member("payload.onnx", f.root / "junction/not-created.onnx"); }, "INTEGRITY_FILE_IDENTITY");
        rejects([&] { shared.copy_member("payload.onnx", f.root / "junction/not-copied.onnx"); }, "INTEGRITY_FILE_IDENTITY");
        check(!exists_os(target / "not-created.onnx") && !exists_os(target / "not-copied.onnx") &&
            info(f.object).links == before_reparse.links, "T0_REPARSE_PARENT_WRITTEN");
        cases.push_back({{"case", "P06-P07-real-noncapacity-collision-copy-no-overwrite-and-junction"}});
    }
    {
        Reset reset; Fixture f(root / "final-directory-collision");
        const auto destination = f.published / "occupied";
        games::tasks::NativePublicationTestAccess::fault([&](const char *phase) {
            if (std::string_view(phase) == "after_space_admission") write(destination / "external-marker", "retain-this");
        });
        rejects([&] { f.publish("occupied"); }, "NATIVE_PUBLICATION_RENAME_FAILED");
        check(platform::file_sha256(destination / "external-marker") == platform::bytes_sha256(
            {reinterpret_cast<const std::uint8_t *>("retain-this"), 11}) && !has_staging(f.published) && info(f.object).links == 1,
            "T0_FINAL_COLLISION_CHANGED_EXTERNAL_DIRECTORY");
        cases.push_back({{"case", "P06-final-directory-collision-preserves-external-marker"}});
    }
    for (const auto phase : {std::string("before_identity"), std::string("after_identity")}) {
        Reset reset; Fixture f(root / phase);
        platform::OwnedBundleDirectoryTestAccess::fault([&](const char *where) {
            if (phase == where) throw std::runtime_error("T0_IDENTITY_FAULT");
        });
        if (phase == "before_identity") {
            bool pending = false;
            try { f.publish("identity"); }
            catch (const platform::DirectoryCleanupPending &e) { pending = e.primary_error() == "T0_IDENTITY_FAULT" && e.cleanup_error() == "OWNED_DIRECTORY_IDENTITY_UNCONFIRMED"; }
            check(pending && has_staging(f.published), "T0_UNKNOWN_IDENTITY_WAS_DELETED_OR_NOT_REPORTED");
        } else {
            rejects([&] { f.publish("identity"); }, "T0_IDENTITY_FAULT");
            check(!has_staging(f.published), "T0_CONFIRMED_IDENTITY_NOT_WITHDRAWN");
        }
        cases.push_back({{"case", "P08-" + phase}});
    }
    {
        Reset reset; Fixture f(root / "primary-and-cleanup-failure"); HANDLE held = INVALID_HANDLE_VALUE;
        fs::path locked_stage; DWORD lock_delete_error{};
        struct Close { HANDLE &handle; ~Close() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } } close{held};
        games::tasks::NativePublicationTestAccess::fault([&](const char *phase) {
            if (std::string_view(phase) != "before_bundle_construction") return;
            const auto stage = f.staging("locked");
            locked_stage = stage;
            held = CreateFileW(platform::extended_path(stage).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            const auto open_error = GetLastError();
            check(held != INVALID_HANDLE_VALUE, "T0_CLEANUP_LOCK_OPEN_FAILED:path=" + platform::utf8(stage) + ";winerror=" + std::to_string(open_error));
            SetLastError(0);
            const auto deletion = CreateFileW(platform::extended_path(stage).c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            lock_delete_error = GetLastError();
            if (deletion != INVALID_HANDLE_VALUE) CloseHandle(deletion);
            check(deletion == INVALID_HANDLE_VALUE && lock_delete_error == ERROR_SHARING_VIOLATION,
                "T0_CLEANUP_LOCK_NOT_EFFECTIVE:path=" + platform::utf8(stage) + ";delete_open_winerror=" + std::to_string(lock_delete_error));
            throw std::runtime_error("T0_PRIMARY_FAILURE");
        });
        bool both = false;
        try { f.publish("locked"); }
        catch (const platform::DirectoryCleanupPending &e) {
            both = e.primary_error() == "T0_PRIMARY_FAILURE" && e.cleanup_error().starts_with("OWNED_DIRECTORY_DELETE_OPEN_FAILED");
            cases.push_back({{"case", "P11-real-cleanup-sharing-denial"}, {"primary_error", e.primary_error()},
                {"cleanup_error", e.cleanup_error()}, {"pending_path", platform::utf8(e.path())},
                {"lock_delete_open_winerror", lock_delete_error}});
        } catch (const std::exception &error) {
            throw std::runtime_error("T0_EXPECTED_PRIMARY_AND_CLEANUP_FAILURE:path=" + platform::utf8(locked_stage) +
                ";delete_open_winerror=" + std::to_string(lock_delete_error) + ";actual=" + error.what());
        }
        check(both, "T0_PRIMARY_OR_CLEANUP_ERROR_LOST"); games::tasks::NativePublicationTestAccess::fault({});
        const auto before = std::distance(directories_os(f.published), fs::directory_iterator{});
        rejects([&] { f.publish("retry"); }, "PREPARATION_CLEANUP_PENDING");
        check(before == std::distance(directories_os(f.published), fs::directory_iterator{}), "T0_BLOCKED_RETRY_CREATED_NEW_UUID");
    }
    {
        Reset reset; Fixture f(root / "nested-object-pending");
        write(f.object.parent_path().parent_path() / ".cleanup-pending-fixture.json", "{\"cleanup_pending\":true}");
        rejects([&] { f.publish("blocked"); }, "PREPARATION_CLEANUP_PENDING");
        check(!has_staging(f.published), "T0_NESTED_PENDING_STARTED_ANOTHER_PUBLICATION");
        cases.push_back({{"case", "P11-nested-pending-blocks-existing-model-path"}});
    }
    {
        Reset reset; Fixture f(root / "rename-sidecar-survives-cleanup-record-failure");
        const auto destination = f.published / "locked-final"; HANDLE held = INVALID_HANDLE_VALUE;
        struct Close { HANDLE &handle; ~Close() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } } close{held};
        DWORD delete_open_error{}; bool cleanup_record_failed = false;
        platform::OwnedBundleDirectoryTestAccess::fault([&](const char *phase) {
            if (std::string_view(phase) == "after_rename" && exists_os(destination)) throw std::runtime_error("T0_AFTER_FINAL_RENAME");
            if (std::string_view(phase) == "before_cleanup") {
                held = CreateFileW(platform::extended_path(destination).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                const auto open_error = GetLastError();
                check(held != INVALID_HANDLE_VALUE, "T0_FINAL_LOCK_OPEN_FAILED:path=" + platform::utf8(destination) + ";winerror=" + std::to_string(open_error));
                SetLastError(0);
                const auto deletion = CreateFileW(platform::extended_path(destination).c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                delete_open_error = GetLastError();
                if (deletion != INVALID_HANDLE_VALUE) CloseHandle(deletion);
                check(deletion == INVALID_HANDLE_VALUE && delete_open_error == ERROR_SHARING_VIOLATION,
                    "T0_FINAL_LOCK_NOT_EFFECTIVE:path=" + platform::utf8(destination) + ";winerror=" + std::to_string(delete_open_error));
            }
            if (std::string_view(phase) == "before_cleanup_marker") { cleanup_record_failed = true; throw std::bad_alloc(); }
        });
        bool both = false;
        try { f.publish("locked-final"); }
        catch (const platform::DirectoryCleanupPending &error) {
            both = error.primary_error() == "T0_AFTER_FINAL_RENAME" && error.cleanup_error().starts_with("OWNED_DIRECTORY_DELETE_OPEN_FAILED");
            cases.push_back({{"case", "P11-pre-rename-durable-sidecar-survives-real-lock-and-cleanup-record-allocation-failure"},
                {"primary_error", error.primary_error()}, {"cleanup_error", error.cleanup_error()},
                {"path", platform::utf8(error.path())}, {"delete_open_winerror", delete_open_error}});
        } catch (const std::exception &error) {
            throw std::runtime_error("T0_EXPECTED_RENAME_CLEANUP_PENDING:path=" + platform::utf8(destination) + ";actual=" + error.what());
        }
        platform::OwnedBundleDirectoryTestAccess::fault({});
        const auto markers = pending_sidecars(f.published);
        check(both && cleanup_record_failed && exists_os(destination / "program/identity.json") && !has_staging(f.published) && markers.size() == 1,
            "T0_FINAL_RENAME_LOST_DURABLE_PENDING_INDEX:path=" + platform::utf8(destination));
        {
            const auto failed = platform::BundleLease::open_publication(destination);
            platform::BundleLease::Manifest expected;
            for (const auto &file : f.baseline.files) expected.emplace(file.relative_path, file.sha256);
            for (const auto &name : {"program/identity.json", "program/flow.json"})
                expected.emplace(name, platform::file_sha256(destination / platform::BundleLease::checked_relative(name)));
            check(failed->manifest() == expected && !failed->resource_digest().empty() &&
                platform::file_sha256(failed->physical_member("models/a.onnx")) == f.hash &&
                platform::file_sha256(failed->physical_member("models/b.onnx")) == f.hash,
                "T0_RETAINED_PENDING_PUBLICATION_CHANGED_LOGICAL_MEMBERS_OR_HASHES");
        } // Inspection lease is released before the blocked-preparation retry.
        const auto marker_hash = platform::file_sha256(markers.front());
        rejects([&] { platform::OwnedBundleDirectory::require_no_pending(f.published); }, "PREPARATION_CLEANUP_PENDING");
        rejects([&] { f.publish("must-not-retry"); }, "PREPARATION_CLEANUP_PENDING");
        check(!exists_os(f.published / "must-not-retry") && !has_staging(f.published) &&
            platform::file_sha256(markers.front()) == marker_hash, "T0_DURABLE_PENDING_INDEX_DID_NOT_BLOCK_AFTER_OWNER_DESTRUCTION");
    }
    {
        Reset reset; Fixture f(root / "accepted-sidecar-remove-failure"); auto publication = f.publish("accepted");
        const auto destination = publication.bundle.root; const auto model_path = publication.bundle.lease->physical_member("models/a.onnx"); const auto original = info(model_path);
        auto receipt = publication.ownership->receipt();
        platform::OwnedBundleDirectoryTestAccess::fault([](const char *phase) {
            if (std::string_view(phase) == "before_prepare_marker_remove") throw std::runtime_error("T0_ACCEPT_INDEX_REMOVE_FAILURE");
            if (std::string_view(phase) == "before_cleanup_marker") throw std::bad_alloc();
        });
        publication.ownership->accept();
        check(receipt->accepted && receipt->cleanup_pending && receipt->marker_failed,
            "T0_ACCEPT_REMOVAL_FAILURE_RECEIPT_NOT_CONSERVATIVE");
        bool acceptance_error = false;
        try { receipt->require_clean_acceptance(); }
        catch (const platform::DirectoryCleanupPending &error) {
            acceptance_error = error.primary_error() == "OWNED_DIRECTORY_ACCEPT_INDEX_FAILED" &&
                error.cleanup_error() == "T0_ACCEPT_INDEX_REMOVE_FAILURE" && receipt->cleanup_error == error.cleanup_error() && error.path() == destination;
        }
        check(acceptance_error, "T0_ACCEPT_REMOVAL_FAILURE_DUAL_ERROR_NOT_REPORTED:path=" + platform::utf8(destination));
        publication.bundle.lease.reset(); publication.ownership.reset(); platform::OwnedBundleDirectoryTestAccess::fault({});
        check(pending_sidecars(f.published).size() == 1 && same_file(original, info(model_path)) &&
            platform::file_sha256(model_path) == f.hash, "T0_ACCEPT_REMOVAL_FAILURE_DELETED_PACKAGE_OR_INDEX");
        rejects([&] { platform::OwnedBundleDirectory::require_no_pending(f.published); }, "PREPARATION_CLEANUP_PENDING");
        rejects([&] { f.publish("must-not-retry"); }, "PREPARATION_CLEANUP_PENDING");
        cases.push_back({{"case", "P11-accepted-package-retained-and-durable-sidecar-blocks-after-index-removal-failure"},
            {"accepted", receipt->accepted}, {"cleanup_pending", receipt->cleanup_pending}, {"marker_failed", receipt->marker_failed},
            {"acceptance_dual_error_reported", acceptance_error},
            {"package_file", info(model_path).json()}});
    }
    {
        Reset reset; Fixture f(root / "prepare-sidecar-create-failure");
        platform::OwnedBundleDirectoryTestAccess::fault([](const char *phase) {
            if (std::string_view(phase) == "before_prepare_marker_create") throw std::runtime_error("T0_PREPARE_INDEX_CREATE_FAILURE");
        });
        rejects([&] { f.publish("not-renamed"); }, "T0_PREPARE_INDEX_CREATE_FAILURE");
        check(!exists_os(f.published / "not-renamed") && !has_staging(f.published) && pending_sidecars(f.published).empty() &&
            info(f.object).links == 1, "T0_FAILED_PREPARE_INDEX_LEFT_PUBLICATION");
        cases.push_back({{"case", "P11-pre-rename-index-creation-failure-withdraws-owned-stage"}});
    }
    {
        Reset reset; Fixture f(root / "admission");
        { auto seed = f.publish("seed"); seed.ownership->accept(); }
        auto probe = f.publish("probe");
        const auto admission = probe.preparation.at("space_admission");
        const auto required = admission.at("required_bytes").get<std::uint64_t>();
        check(admission.at("logical_copy_bytes").get<std::uint64_t>() >= 2 * f.model.size() &&
            admission.at("missing_object_bytes") == 0 && probe.preparation.at("source_model_bytes_after_copy") == 0,
            "T0_ALIAS_BUDGET_NOT_LOGICAL_OR_MODEL_EAGER");
        probe.bundle.lease.reset(); probe.ownership->rollback();
        games::tasks::NativePublicationTestAccess::available(required - 1);
        rejects([&] { f.publish("low"); }, "NATIVE_PUBLICATION_SPACE_LOW");
        check(!has_staging(f.published) && !exists_os(f.published / "low"), "T0_LOW_SPACE_CREATED_STAGE");
        games::tasks::NativePublicationTestAccess::available((std::numeric_limits<std::uint64_t>::max)());
        rejects([&] { f.publish("unknown"); }, "NATIVE_PUBLICATION_SPACE_UNKNOWN");
        games::tasks::NativePublicationTestAccess::available(required);
        auto exact = f.publish("exact"); exact.ownership->accept();
        std::uint64_t total = (std::numeric_limits<std::uint64_t>::max)() - 1;
        rejects([&] { games::tasks::NativePublicationTestAccess::add(total, 2); }, "NATIVE_PUBLICATION_BUDGET_OVERFLOW");
        check(total == (std::numeric_limits<std::uint64_t>::max)() - 1, "T0_OVERFLOW_CHANGED_ACCUMULATOR");
        rejects([&] { games::tasks::NativePublicationTestAccess::round((std::numeric_limits<std::uint64_t>::max)()); }, "NATIVE_PUBLICATION_BUDGET_OVERFLOW");
        check(games::tasks::NativePublicationTestAccess::round(0) == 0 &&
            games::tasks::NativePublicationTestAccess::round(4097) == 8192, "T0_ALIGNMENT_BOUNDARY_WRONG");
        cases.push_back({{"case", "P12-alias-size-exact-low-unknown-space-and-actual-overflow-helpers"}, {"admission", admission}});
    }
    {
        Reset reset; const auto area = root / "preoccupied-owner"; const auto directory = area / "occupied";
        write(directory / "external-marker", "keep"); const auto hash = platform::file_sha256(directory / "external-marker");
        rejects([&] { platform::OwnedBundleDirectory::create(directory); }, "OWNED_DIRECTORY_ALREADY_EXISTS");
        check(platform::file_sha256(directory / "external-marker") == hash, "T0_PREOCCUPIED_OWNER_DELETED_EXTERNAL_FILE");
        cases.push_back({{"case", "P15-exclusive-create-does-not-claim-preoccupied-directory"}});
    }
    {
        Reset reset; Fixture f(root / "paths");
        platform::BundleLease shared(platform::extended_path(f.object.parent_path()), f.hash, {{"payload.onnx", f.hash}});
        const auto dos = root / fs::path(L"中文 空格") / "dos"; mkdirs_os(dos);
        shared.link_member("payload.onnx", dos / "linked.onnx"); shared.copy_member("payload.onnx", dos / "copied.onnx");
        check(same_file(info(dos / "linked.onnx"), info(f.object)) && !same_file(info(dos / "copied.onnx"), info(f.object)), "T0_DOS_COPY_LINK_IDENTITY_WRONG");
        auto long_root = root / fs::path(L"扩展 长路径 空格");
        while (long_root.native().size() < 290) long_root /= L"abcdefghijklmnop";
        long_root = platform::extended_path(long_root); mkdirs_os(long_root);
        shared.link_member("payload.onnx", long_root / "linked.onnx"); shared.copy_member("payload.onnx", long_root / "copied.onnx");
        check(platform::file_sha256(long_root / "linked.onnx") == f.hash && platform::file_sha256(long_root / "copied.onnx") == f.hash,
            "T0_EXTENDED_LONG_COPY_LINK_FAILED");
        cases.push_back({{"case", "P14-DOS-and-extended-long-Chinese-space-copy-link"}, {"extended_characters", long_root.native().size()}, {"unc", "not_provided_not_tested"}});
        auto long_dos = root / fs::path(L"普通 DOS 中文 空格 长路径");
        while (long_dos.native().size() < 290) long_dos /= L"abcdefghijklmnop";
        check(!long_dos.native().starts_with(L"\\\\?\\"), "T0_DOS_LONG_FIXTURE_ALREADY_EXTENDED");
        mkdirs_os(platform::extended_path(long_dos));
        shared.link_member("payload.onnx", long_dos / "linked.onnx"); shared.copy_member("payload.onnx", long_dos / "copied.onnx");
        check(same_file(info(long_dos / "linked.onnx"), info(f.object)) && !same_file(info(long_dos / "copied.onnx"), info(f.object)) &&
            platform::file_sha256(platform::extended_path(long_dos / "copied.onnx")) == f.hash,
            "T0_DOS_LONG_COPY_LINK_FAILED:path=" + platform::utf8(long_dos));
        {
            platform::BundleLease frozen(long_dos, f.hash, {{"linked.onnx", f.hash}, {"copied.onnx", f.hash}});
            frozen.verify_members();
            check(frozen.root() == long_dos && frozen.storage_stats().model_bytes == 0, "T0_DOS_LONG_FROZEN_ROOT_OR_STORAGE_CHANGED");
        }
        const auto stage = long_dos / "owner.staging-regression", destination = long_dos / "owner-final";
        auto owner = platform::OwnedBundleDirectory::create(stage);
        write(platform::extended_path(stage / "marker.txt"), "owned-long-path");
        const auto before = info(stage / "marker.txt");
        owner->rename_to(destination);
        check(owner->receipt()->path == destination && same_file(before, info(destination / "marker.txt")),
            "T0_DOS_LONG_OWNER_RENAME_IDENTITY_OR_PUBLIC_PATH_CHANGED");
        owner->rollback(); owner.reset();
        check(!exists_os(platform::extended_path(stage)) && !exists_os(platform::extended_path(destination)), "T0_DOS_LONG_OWNER_ROLLBACK_LEFT_PACKAGE");
        platform::OwnedBundleDirectory::require_no_pending(long_dos);
        cases.push_back({{"case", "P14-ordinary-DOS-over260-copy-link-owner-create-rename-rollback"},
            {"dos_characters", long_dos.native().size()}, {"owner_public_path", platform::utf8(destination)},
            {"marker_before_rename", before.json()}, {"rolled_back", true}});
        const auto full_destination = long_dos / "full-publication";
        auto full = games::tasks::publish_native(f.workflow, f.baseline, full_destination, J::object());
        full.bundle.lease->verify_members();
        check(full.bundle.root == full_destination && full.bundle.lease->root() == full_destination &&
            platform::file_sha256(full.bundle.lease->physical_member("models/a.onnx")) == f.hash &&
            platform::file_sha256(full.bundle.lease->physical_member("models/b.onnx")) == f.hash &&
            full.bundle.lease->storage_stats().model_bytes == 0, "T0_DOS_LONG_FULL_PUBLICATION_ROOT_HASH_OR_STORAGE_CHANGED");
        full.ownership->accept(); full.ownership->receipt()->require_clean_acceptance();
        cases.push_back({{"case", "P14-ordinary-DOS-over260-full-publication-and-frozen-manifest"},
            {"public_root", platform::utf8(full.bundle.root)}, {"lease_root", platform::utf8(full.bundle.lease->root())},
            {"model_eager_bytes", full.bundle.lease->storage_stats().model_bytes}, {"sha256", f.hash},
            {"accepted", full.ownership->receipt()->accepted}});
    }
    write(root / "receipt.json", J{{"complete", true}, {"device_actions", false}, {"filesystem", "NTFS"}, {"cases", cases},
        {"io_failure_scope", "internal write/flush fault injection; real readonly WriteFile failure retained in original suite"},
        {"unc", "not_provided_not_tested"}}.dump(2));
    return cases;
}
} // namespace publication_t0_contract
