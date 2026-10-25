#pragma once
#include "storage/submission_store.hpp"
#include "atomic_write_test_access.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <winioctl.h>

namespace wvd::storage {
struct SubmissionStoreTestAccess {
    static std::unique_ptr<SubmissionStore> small(const std::filesystem::path &root,
        std::size_t count = 3, std::size_t scan = 8, std::uintmax_t bytes = 16384) {
        return std::unique_ptr<SubmissionStore>(new SubmissionStore(root,
            SubmissionStore::Limits{1024, bytes, count, scan}));
    }
};
}

namespace submission_contract {
using J = nlohmann::json;
using Store = wvd::storage::SubmissionStore;
using Access = wvd::storage::SubmissionStoreTestAccess;
using Fault = wvd::platform::AtomicWriteTestAccess;
inline void check(bool ok, const std::string &code) { if (!ok) throw std::runtime_error(code); }
inline std::filesystem::path record(const std::filesystem::path &root, const std::string &id) {
    return root / (wvd::platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(id.data()), id.size()}) + ".json");
}
inline std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
inline std::size_t entry_count(const std::filesystem::path &root) {
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(root),
        std::filesystem::directory_iterator{}));
}
template<class Call> void rejects(Call call, const std::string &prefix) {
    bool rejected{};
    try { call(); }
    catch (const std::exception &e) { rejected = std::string(e.what()).starts_with(prefix); }
    check(rejected, "SUBMISSION_EXPECTED_REJECTION:" + prefix);
}
inline void junction(const std::filesystem::path &link, const std::filesystem::path &target) {
    std::filesystem::create_directory(link);
    const auto substitute = std::wstring(L"\\??\\") + target.native();
    const auto display = target.native();
    struct Mount {
        ULONG tag; USHORT length, reserved;
        USHORT substitute_offset, substitute_length, print_offset, print_length;
        WCHAR names[1];
    };
    const auto length = offsetof(Mount, names) + (substitute.size() + display.size() + 2) * sizeof(WCHAR);
    std::vector<std::max_align_t> storage((length + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *mount = reinterpret_cast<Mount *>(storage.data());
    mount->tag = IO_REPARSE_TAG_MOUNT_POINT;
    mount->length = static_cast<USHORT>(length - 8);
    mount->substitute_offset = 0; mount->substitute_length = static_cast<USHORT>(substitute.size() * 2);
    mount->print_offset = static_cast<USHORT>((substitute.size() + 1) * 2);
    mount->print_length = static_cast<USHORT>(display.size() * 2);
    std::memcpy(mount->names, substitute.c_str(), (substitute.size() + 1) * 2);
    std::memcpy(reinterpret_cast<char *>(mount->names) + mount->print_offset,
        display.c_str(), (display.size() + 1) * 2);
    const auto h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    check(h != INVALID_HANDLE_VALUE, "SUBMISSION_JUNCTION_OPEN_FAILED");
    DWORD written{};
    const auto ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, mount, static_cast<DWORD>(length),
        nullptr, 0, &written, nullptr);
    const auto code = GetLastError(); CloseHandle(h);
    check(ok != FALSE, "SUBMISSION_JUNCTION_CREATE_FAILED:" + std::to_string(code));
}
inline void crash_child(const std::filesystem::path &root, bool replace) {
    wchar_t executable[32768]{};
    check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "SUBMISSION_CHILD_EXE_FAILED");
    auto command = L"\"" + std::wstring(executable) + L"\" --submission-crash \"" + root.native() +
        L"\" " + (replace ? L"replace" : L"new");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process) != FALSE, "SUBMISSION_CHILD_START_FAILED");
    CloseHandle(process.hThread);
    const auto waited = WaitForSingleObject(process.hProcess, 30000);
    if (waited != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 99); WaitForSingleObject(process.hProcess, 5000); }
    DWORD exit{}; GetExitCodeProcess(process.hProcess, &exit); CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0 && exit == 73, "SUBMISSION_CHILD_DID_NOT_EXIT_BEFORE_RENAME");
}
inline int interrupted(const std::filesystem::path &root, bool replace) {
    Store store(root);
    Fault::install([](auto phase, const auto &) {
        if (phase == Fault::Phase::Commit) std::_Exit(73);
        return false;
    });
    const std::string id = replace ? "old" : "new";
    store.save(id, {{"kind", "isolated"}}, {{"state", "completed"}, {"run_id", 99}}, replace);
    throw std::runtime_error("SUBMISSION_CHILD_COMMITTED_INSTEAD_OF_EXITING");
}
__declspec(noinline) inline void run(const std::filesystem::path &root) {
    check(!std::filesystem::exists(root), "SUBMISSION_BOUNDARY_ROOT_MUST_BE_FRESH");
    std::filesystem::create_directories(root);
    J evidence = J::array();
    const J intent{{"kind", "isolated"}};
    const J completed{{"state", "completed"}, {"run_id", 7}};
    {
        const auto path = root / "small-boundary";
        {
            auto store = Access::small(path);
            store->save("one", intent, completed, false);
            store->save("two", intent, completed, false);
            check(store->status().at("committed_count") == 2, "COUNT_BEFORE_BOUNDARY");
            store->save("three", intent, completed, false);
            const auto before = entry_count(path);
            rejects([&] { store->save("four", intent, completed, false); }, "REQUEST_HISTORY_COUNT_LIMIT");
            check(entry_count(path) == before && !std::filesystem::exists(record(path, "four")), "COUNT_REJECTION_CREATED_INTENT");
            store->save("one", intent, {{"state", "submitted"}, {"run_id", 8}}, true);
            check(store->status().at("committed_count") == 3 && store->replay("one", intent)->at("run_id") == 8,
                "REPLACEMENT_CHANGED_COUNT_OR_RECEIPT");
        }
        auto restarted = Access::small(path);
        check(!restarted->status().at("read_only").get<bool>() && restarted->status().at("committed_count") == 3,
            "COUNT_BOUNDARY_RESTART_CHANGED_STATE");
        rejects([&] { restarted->save("four", intent, completed, false); }, "REQUEST_HISTORY_COUNT_LIMIT");
        evidence.push_back({{"case", "count-boundary-and-replace-restart"}, {"status", restarted->status()}});
    }
    {
        const auto path = root / "real-constant";
        std::filesystem::create_directory(path);
        // Historical fixture rows are real bytes. Only the next row uses the
        // writer being tested, so this checks startup accounting at the actual
        // production boundary without 65,535 redundant fsync calls.
        for (std::size_t index = 0; index < 65535; ++index) {
            const auto id = "history-" + std::to_string(index);
            std::ofstream file(record(path, id), std::ios::binary);
            file << J{{"schema", 1}, {"request_id", id}, {"intent", intent}, {"receipt", completed}}.dump();
            file.close(); check(bool(file), "REAL_COUNT_FIXTURE_WRITE_FAILED");
        }
        Store store(path);
        check(store.status().at("count_limit") == 65536 && store.status().at("committed_count") == 65535 &&
            !store.status().at("read_only").get<bool>(), "REAL_COUNT_BEFORE_BOUNDARY");
        store.save("65536th", intent, completed, false);
        check(store.status().at("committed_count") == 65536, "REAL_COUNT_EXACT_BOUNDARY");
        const auto before = entry_count(path);
        rejects([&] { store.save("65537th", intent, completed, false); }, "REQUEST_HISTORY_COUNT_LIMIT");
        check(entry_count(path) == before && !std::filesystem::exists(record(path, "65537th")), "REAL_COUNT_NEXT_INTENT_CREATED");
        store.save("history-0", intent, completed, true);
        check(store.status().at("committed_count") == 65536, "REAL_COUNT_REPLACE_ADDED_COUNT");
        Store restarted(path);
        check(restarted.status().at("committed_count") == 65536 && !restarted.status().at("read_only").get<bool>() &&
            restarted.replay("65536th", intent)->at("run_id") == 7, "REAL_COUNT_RESTART_CHANGED_ACCOUNTING");
        rejects([&] { restarted.save("65537th", intent, completed, false); }, "REQUEST_HISTORY_COUNT_LIMIT");
        check(entry_count(path) == before, "REAL_COUNT_RESTART_REJECTION_CREATED_INTENT");
        evidence.push_back({{"case", "real-constant-65535-65536-next-and-restart"}, {"status", restarted.status()}});
    }
    {
        const auto path = root / "byte-boundary";
        auto store = Access::small(path, 3, 8, 2048);
        J receipt{{"state", "completed"}, {"padding", ""}};
        const auto base = J{{"schema", 1}, {"request_id", "max"}, {"intent", intent}, {"receipt", receipt}}.dump().size();
        receipt["padding"] = std::string(1024 - base, 'x');
        store->save("max", intent, receipt, false);
        check(store->status().at("committed_bytes") == 1024, "BYTE_BOUNDARY_NOT_EXACT");
        const auto before = entry_count(path);
        rejects([&] { store->save("next", intent, completed, false); }, "REQUEST_HISTORY_STORAGE_LIMIT");
        check(entry_count(path) == before, "BYTE_REJECTION_CREATED_TEMP");
        store->save("max", intent, completed, true);
        store->save("next", intent, completed, false);
        check(store->status().at("committed_count") == 2, "BYTE_REPLACEMENT_ACCOUNTING_FAILED");
        evidence.push_back({{"case", "byte-boundary-and-replacement-reserve"}, {"status", store->status()}});
    }
    for (const auto phase : {Fault::Phase::Write, Fault::Phase::Flush, Fault::Phase::Commit}) {
        for (const bool replace : {false, true}) {
            const auto name = "fault-" + std::to_string(static_cast<int>(phase)) + (replace ? "-replace" : "-new");
            const auto path = root / name;
            {
                Store store(path); store.save("old", intent, completed, false);
                const auto before = read_text(record(path, "old"));
                unsigned calls{};
                Fault::install([&](auto at, const auto &target) {
                    if (target.parent_path() == path && at == phase) { ++calls; return true; }
                    return false;
                });
                struct Clear { ~Clear() { Fault::clear(); } } clear;
                rejects([&] { store.save(replace ? "old" : "new", intent,
                    {{"state", "completed"}, {"run_id", 99}}, replace); }, "STORAGE_COMMIT_FAILED");
                check(calls == 1 && read_text(record(path, "old")) == before, "WRITE_FAILURE_CHANGED_OLD_BYTES");
                const auto status = store.status();
                check(status.at("committed_count") == 1 && status.at("pending_count") == 1 && status.at("read_only") == true,
                    "WRITE_FAILURE_ACCOUNTING_LOST");
                std::uintmax_t bytes{};
                for (const auto &file : std::filesystem::directory_iterator(path)) bytes += file.file_size();
                check(status.at("committed_bytes").get<std::uintmax_t>() + status.at("pending_bytes").get<std::uintmax_t>() == bytes,
                    "FAILED_TEMP_NOT_COUNTED");
                rejects([&] { store.save("later", intent, completed, false); }, "REQUEST_HISTORY_READ_ONLY");
                check(store.replay("old", intent)->at("run_id") == 7, "READONLY_OLD_RECEIPT_UNAVAILABLE");
                evidence.push_back({{"case", name}, {"status", status}});
            }
            Store restarted(path);
            check(restarted.status().at("read_only") == true && restarted.status().at("pending_count") == 1 &&
                restarted.replay("old", intent)->at("run_id") == 7, "TEMP_RESTART_BLOCKED_HISTORY_READ");
        }
    }
    {
        const auto path = root / "real-rename-sharing-failure";
        Store store(path); store.save("old", intent, completed, false);
        const auto before = read_text(record(path, "old"));
        const auto lock = CreateFileW(record(path, "old").c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        check(lock != INVALID_HANDLE_VALUE, "RENAME_FAILURE_LOCK_UNAVAILABLE");
        rejects([&] { store.save("old", intent, {{"state", "completed"}, {"run_id", 99}}, true); }, "STORAGE_COMMIT_FAILED");
        CloseHandle(lock);
        check(store.status().at("pending_count") == 1 && read_text(record(path, "old")) == before,
            "REAL_RENAME_FAILURE_CHANGED_OLD");
        evidence.push_back({{"case", "real-rename-sharing-failure"}, {"status", store.status()}});
    }
    for (const bool replace : {false, true}) {
        const auto path = root / (replace ? "crash-replace" : "crash-new");
        { Store store(path); store.save("old", intent, completed, false); }
        const auto before = read_text(record(path, "old")); crash_child(path, replace);
        Store restarted(path);
        check(read_text(record(path, "old")) == before && restarted.status().at("pending_count") == 1 &&
            restarted.status().at("read_only") == true && restarted.replay("old", intent)->at("run_id") == 7,
            "CRASH_BEFORE_RENAME_LOST_HISTORY");
        rejects([&] { restarted.save("later", intent, completed, false); }, "REQUEST_HISTORY_READ_ONLY");
        evidence.push_back({{"case", replace ? "crash-replace" : "crash-new"}, {"status", restarted.status()}});
    }
    {
        const auto path = root / "legacy-oversized";
        { Store store(path); for (int i = 0; i < 4; ++i) store.save("legacy-" + std::to_string(i), intent, completed, false); }
        auto limited = Access::small(path);
        check(limited->status().at("read_only") == true && limited->status().at("committed_count") == 4 &&
            limited->replay("legacy-0", intent)->at("run_id") == 7, "LEGACY_OVER_COUNT_HISTORY_UNAVAILABLE");
        evidence.push_back({{"case", "legacy-over-limit"}, {"status", limited->status()}});
        limited.reset(); auto bounded = Access::small(path, 3, 2);
        check(bounded->status().at("scanned_entries") == 2 && bounded->status().at("capacity_known") == false &&
            bounded->status().at("scan_complete") == false && bounded->replay("legacy-3", intent)->at("run_id") == 7,
            "LEGACY_SCAN_NOT_BOUNDED_OR_OLD_ID_UNREADABLE");
        evidence.push_back({{"case", "bounded-legacy-scan"}, {"status", bounded->status()}});
    }
    {
        const auto path = root / "unknown-entry";
        { Store store(path); store.save("old", intent, completed, false); }
        { std::ofstream unknown(path / std::filesystem::path(L"未核验 文件.tmp")); unknown << "not a recognized atomic write"; }
        Store restarted(path);
        check(restarted.status().at("unknown_entries") == 1 && restarted.status().at("pending_count") == 0 &&
            restarted.status().at("read_only") == true && restarted.replay("old", intent)->at("run_id") == 7,
            "UNKNOWN_ENTRY_PROMOTED_OR_HISTORY_LOST");
        evidence.push_back({{"case", "unknown-entry"}, {"status", restarted.status()}});
    }
    {
        const auto path = root / "pending-intent";
        { Store store(path); store.save("old", intent, {{"state", "preparing"}}, false); }
        Store restarted(path);
        check(restarted.status().at("pending_intent_count") == 1 && restarted.status().at("read_only") == true &&
            restarted.replay("old", intent)->at("state") == "preparing", "PREPARING_INTENT_REEXECUTED_OR_REWRITTEN");
        evidence.push_back({{"case", "pending-intent"}, {"status", restarted.status()}});
    }
    {
        const auto path = root / "unreadable-size";
        { Store store(path); store.save("old", intent, completed, false); }
        // A foreign directory has no bounded, trusted receipt-file size; do
        // not recurse into it or report its unknown contents as zero bytes.
        const auto unknown = path / "unverified-directory";
        std::filesystem::create_directory(unknown);
        { std::ofstream marker(unknown / "foreign-data.bin"); marker << "preserve"; }
        { Store restarted(path); check(restarted.status().at("read_only") == true &&
            restarted.status().at("capacity_known") == false && restarted.status().at("unknown_bytes").is_null() &&
            restarted.replay("old", intent)->at("run_id") == 7, "UNKNOWN_SIZE_WAS_ZERO_OR_READ_LOST");
          evidence.push_back({{"case", "unverified-size"}, {"status", restarted.status()}}); }
        check(read_text(unknown / "foreign-data.bin") == "preserve", "FOREIGN_DIRECTORY_CHANGED");
    }
    {
        const auto target = root / "junction-target"; std::filesystem::create_directory(target);
        const auto link = root / "junction"; junction(link, target);
        rejects([&] { Store invalid(link / "must-not-create"); }, "REQUEST_HISTORY_REPARSE_POINT");
        check(!std::filesystem::exists(target / "must-not-create"), "UNTRUSTED_ANCESTOR_WAS_TRAVERSED");
        evidence.push_back({{"case", "junction-root-rejected"}});
    }
    std::ofstream(root / "results.json") << evidence.dump(2);
    std::cout << "PASS real SubmissionStore: bounded readonly recovery, count/bytes gates, native atomic-write faults, real rename lock, process exits, junction rejection\n";
}
}
