#pragma once
#include "storage/run_store.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/runtime_files.hpp"
#include "atomic_write_test_access.hpp"
#include <opencv2/imgcodecs.hpp>
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace recent_frames_contract {
using J = nlohmann::json;
using namespace wvd;
using namespace std::chrono_literals;
inline void require(bool value, const char *code) { if (!value) throw std::runtime_error(code); }
inline J definition() {
    return {{"engine_kind", "wvd_native"}, {"device_id", "recent-fixture"}, {"game_id", "wvd"},
        {"pack_revision", "recent-fixture"}, {"viewport", "900x1600"}};
}
inline contracts::FrameEnvelope frame(std::uint64_t id = 1) {
    contracts::FrameEnvelope result;
    auto &identity = result.identity;
    identity.device_id = "recent-fixture"; identity.game_id = "wvd";
    identity.pack_revision = "recent-fixture"; identity.viewport_id = "900x1600";
    identity.generation = identity.connection_generation = 1;
    identity.frame_id = id; identity.action_epoch = id;
    identity.raw_size = identity.recognition_size = {900, 1600};
    identity.captured_at = std::chrono::steady_clock::now(); identity.backend = "isolated-recent-fixture";
    result.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(900 * 1600 * 3, 81);
    return result;
}
inline std::pair<std::size_t, std::uint64_t> occupancy(const std::filesystem::path &directory) {
    std::pair<std::size_t, std::uint64_t> result{};
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        require(entry.is_regular_file(), "RECENT_TEST_UNEXPECTED_DIRECTORY");
        ++result.first; result.second += entry.file_size();
    }
    return result;
}
inline void write_bytes(const std::filesystem::path &file, std::size_t size) {
    std::ofstream output(file, std::ios::binary);
    const std::string block(65536, 'x');
    for (std::size_t written = 0; written < size;) {
        const auto count = (std::min)(block.size(), size - written);
        output.write(block.data(), static_cast<std::streamsize>(count)); written += count;
    }
    require(bool(output), "RECENT_TEST_PREFILL_FAILED");
}
inline std::filesystem::path old_name(const std::filesystem::path &directory, unsigned id) {
    return directory / ("20200101T000000000_r1_f" + std::to_string(10000 + id) + ".png");
}
inline void prefill(const std::filesystem::path &directory, unsigned count, std::size_t bytes) {
    std::filesystem::create_directories(directory);
    for (unsigned i = 0; i < count; ++i) write_bytes(old_name(directory, i), bytes);
}
inline J recent(const storage::RunStore &store) { return store.diagnostic_summary(true).at("recent_frames"); }
inline bool enqueue(storage::RunStore &store, const contracts::FrameEnvelope &picture, bool action = true) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    do {
        if (store.save_recent_frame(picture, action)) return true;
        if (recent(store).at("blocked").get<bool>()) return false;
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
inline void wait_blocked(storage::RunStore &store) {
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (!recent(store).at("blocked").get<bool>() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(10ms);
    require(recent(store).at("blocked").get<bool>(), "RECENT_TEST_WRITER_NOT_BLOCKED");
}
inline void wait_saved(storage::RunStore &store, std::uint64_t wanted) {
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (recent(store).at("saved").get<std::uint64_t>() < wanted &&
        !recent(store).at("blocked").get<bool>() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(10ms);
    require(recent(store).at("saved").get<std::uint64_t>() == wanted, "RECENT_TEST_SAVE_DID_NOT_COMPLETE");
}
inline void marker(const std::filesystem::path &root) {
    std::ofstream(root / "wp03-fixture.json") << J{{"kind", "disposable-wp03-recent"}}.dump();
}
inline int reopen(const std::filesystem::path &root) {
    std::ifstream input(root / "wp03-fixture.json"); J owner; input >> owner;
    require(owner == J{{"kind", "disposable-wp03-recent"}}, "RECENT_TEST_OWNER_REQUIRED");
    const auto before = occupancy(root / "recent-frames");
    storage::RunStore store(root / "runs", "restarted-process", 1, definition());
    if (!recent(store).at("blocked").get<bool>()) {
        require(enqueue(store, frame(999)), "RECENT_TEST_RESTART_NOT_QUEUED");
        wait_blocked(store);
    }
    for (unsigned i = 0; i < 20; ++i)
        require(!store.save_recent_frame(frame(1000 + i), true), "RECENT_TEST_RESTART_BYPASSED_BLOCK");
    store.finish_recent_frames();
    require(occupancy(root / "recent-frames") == before, "RECENT_TEST_RESTART_GREW_DIRECTORY");
    std::ofstream(root / "process-restart.json") << recent(store).dump(2);
    return 0;
}
inline void check_process_restart(const std::filesystem::path &root) {
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, 32768) > 0, "RECENT_TEST_EXE_MISSING");
    auto command = L"\"" + std::wstring(executable) + L"\" --recent-frame-reopen \"" + root.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process), "RECENT_TEST_CHILD_CREATE_FAILED");
    CloseHandle(process.hThread);
    const auto waited = WaitForSingleObject(process.hProcess, 20000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 90);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD code{}; const auto queried = GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    require(waited == WAIT_OBJECT_0 && queried && code == 0, "RECENT_TEST_CHILD_FAILED");
}
inline int run(const std::filesystem::path &root) {
    require(root.is_absolute() && !std::filesystem::exists(root), "RECENT_TEST_FRESH_ROOT_REQUIRED");
    std::filesystem::create_directories(root);
    J report = J::array();
    // Real Windows deletion sharing denial at both actual policy boundaries.
    for (const auto &mode : {std::string("count"), std::string("bytes")}) {
        const auto area = root / mode, directory = area / "recent-frames";
        const auto count = mode == "count" ? 240u : 32u;
        prefill(directory, count, mode == "count" ? 64u : 4u * 1024 * 1024);
        marker(area);
        const auto before = occupancy(directory);
        const auto hash = platform::file_sha256(old_name(directory, 0));
        const auto held = CreateFileW(platform::extended_path(old_name(directory, 0)).c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "RECENT_TEST_OLDEST_LOCK_FAILED");
        struct Close { HANDLE handle; ~Close() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } } close{held};
        {
            storage::RunStore store(area / "runs", "blocked-worker", 1, definition());
            require(enqueue(store, frame()), "RECENT_TEST_BOUNDARY_NOT_QUEUED");
            wait_blocked(store);
            for (unsigned i = 0; i < 20; ++i)
                require(!store.save_recent_frame(frame(10 + i), true), "RECENT_TEST_BLOCKED_WRITER_ACCEPTED");
            store.finish_recent_frames();
            require(occupancy(directory) == before && platform::file_sha256(old_name(directory, 0)) == hash,
                "RECENT_TEST_LOCK_FAILURE_GREW_OR_CHANGED_HISTORY");
            require(recent(store).at("blocked_reason") == "RECENT_FRAME_TRIM_FAILED", "RECENT_TEST_WRONG_TRIM_REASON");
            report.push_back({{"case", mode + "-locked-trim"}, {"summary", recent(store)}});
        }
        {
            storage::RunStore store(area / "runs", "rebuilt-worker", 1, definition());
            require(enqueue(store, frame(50)), "RECENT_TEST_REBUILT_NOT_QUEUED");
            wait_blocked(store); store.finish_recent_frames();
            require(occupancy(directory) == before, "RECENT_TEST_NEW_STORE_RESET_QUOTA");
        }
        check_process_restart(area);
        CloseHandle(close.handle); close.handle = INVALID_HANDLE_VALUE;
        storage::RunStore recovered(area / "runs", "explicit-revalidation", 1, definition());
        require(recovered.revalidate_recent_frames(), "RECENT_TEST_EXPLICIT_REVALIDATION_FAILED");
        require(enqueue(recovered, frame(70)), "RECENT_TEST_RECOVERY_NOT_QUEUED");
        wait_saved(recovered, 1); recovered.finish_recent_frames();
        const auto after = occupancy(directory);
        require(after.first <= 240 && after.second <= 128ULL * 1024 * 1024,
            "RECENT_TEST_RECOVERY_EXCEEDED_POLICY");
        report.push_back({{"case", mode + "-released-and-revalidated"}, {"summary", recent(recovered)}});
    }
    for (const auto phase : {platform::AtomicWriteTestAccess::Phase::Write,
                            platform::AtomicWriteTestAccess::Phase::Flush,
                            platform::AtomicWriteTestAccess::Phase::Commit}) {
        const auto label = phase == platform::AtomicWriteTestAccess::Phase::Write ? "write" :
            phase == platform::AtomicWriteTestAccess::Phase::Flush ? "flush" : "rename";
        const auto area = root / label, directory = area / "recent-frames";
        prefill(directory, 1, 64); marker(area);
        const auto old_hash = platform::file_sha256(old_name(directory, 0));
        storage::RunStore store(area / "runs", "atomic-failure", 1, definition());
        platform::AtomicWriteTestAccess::install([=](auto actual, const auto &target) {
            return actual == phase && target.parent_path() == directory;
        });
        struct Clear { ~Clear() { platform::AtomicWriteTestAccess::clear(); } } clear;
        require(enqueue(store, frame()), "RECENT_TEST_FAILURE_NOT_QUEUED");
        wait_blocked(store); platform::AtomicWriteTestAccess::clear();
        for (unsigned i = 0; i < 20; ++i)
            require(!store.save_recent_frame(frame(10 + i), true), "RECENT_TEST_ATOMIC_FAILURE_RETRIED");
        const auto metadata = recent(store);
        require(metadata.at("occupancy_known") == true && metadata.at("incomplete_files") == 1 &&
            metadata.at("files") == 2 && platform::file_sha256(old_name(directory, 0)) == old_hash,
            "RECENT_TEST_TEMP_NOT_COUNTED_OR_OLD_CHANGED");
        const auto before = occupancy(directory);
        require(before.first == 2 && metadata.at("bytes") == before.second,
            "RECENT_TEST_TEMP_BYTES_NOT_COUNTED");
        check_process_restart(area);
        storage::DiagnosticRequest request; request.run_id = request.generation = 1; request.reason = "RECENT_TEST_CRITICAL";
        request.critical = true; request.node = "isolated"; request.stage = "pre_action";
        auto evidence = frame(90);
        require(store.save_diagnostic(&evidence, request).at("status") == "saved",
            "RECENT_TEST_BLOCK_DISABLED_CRITICAL_DIAGNOSTIC");
        report.push_back({{"case", std::string(label) + "-failure"}, {"summary", metadata}});
        // Resolve only this test's one fault-injected temporary, with its exact
        // identity already enumerated above; no production cleanup is exposed.
        for (const auto &entry : std::filesystem::directory_iterator(directory))
            if (entry.path().extension() == ".tmp") require(std::filesystem::remove(entry.path()), "RECENT_TEST_TMP_CLEANUP_FAILED");
        require(store.revalidate_recent_frames(), "RECENT_TEST_RESOLVED_TEMP_STILL_BLOCKED");
        require(enqueue(store, frame(100)), "RECENT_TEST_UNBLOCKED_NOT_QUEUED");
        wait_saved(store, 1); store.finish_recent_frames();
    }
    {
        const auto area = root / "normal", directory = area / "recent-frames";
        storage::RunStore store(area / "runs", "normal", 1, definition());
        auto picture = frame(); require(enqueue(store, picture, false), "RECENT_TEST_JPG_NOT_QUEUED");
        require(!store.save_recent_frame(frame(2)), "RECENT_TEST_JPG_THROTTLE_LOST");
        picture.identity.frame_id = 3;
        require(enqueue(store, picture) && enqueue(store, picture), "RECENT_TEST_ACTION_DEDUP_LOST");
        store.finish_recent_frames(); unsigned png = 0, jpg = 0;
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".jpg") ++jpg;
            if (entry.path().extension() == ".png") {
                ++png; const auto decoded = cv::imread(entry.path().string());
                require(!decoded.empty() && cv::norm(decoded, cv::Mat(1600, 900, CV_8UC3, cv::Scalar(81,81,81)), cv::NORM_INF) == 0,
                    "RECENT_TEST_ACTION_NOT_LOSSLESS");
            }
        }
        require(png == 1 && jpg == 1 && recent(store).at("saved") == 2 && recent(store).at("failed") == 0,
            "RECENT_TEST_NORMAL_REGRESSION");
        report.push_back({{"case", "normal-png-jpg-throttle-flush-dedup"}, {"summary", recent(store)}});
    }
    {
        const auto area = root / "unknown", directory = area / "recent-frames";
        std::filesystem::create_directories(directory); write_bytes(directory / "user-file.txt", 11);
        storage::RunStore store(area / "runs", "unknown", 1, definition());
        require(recent(store).at("blocked") == true && !store.save_recent_frame(frame(), true) &&
            std::filesystem::file_size(directory / "user-file.txt") == 11, "RECENT_TEST_UNKNOWN_ENTRY_NOT_PROTECTED");
        store.finish_recent_frames(); report.push_back({{"case", "unknown-entry-preserved"}, {"summary", recent(store)}});
    }
    {
        const auto area = root / "pending-byte-quota", directory = area / "recent-frames";
        std::filesystem::create_directories(directory); marker(area);
        for (unsigned i = 0; i < 34; ++i) {
            auto path = old_name(directory, i); path += "." + platform::unique_id() + ".tmp";
            write_bytes(path, 4 * 1024 * 1024);
        }
        const auto before = occupancy(directory);
        storage::RunStore store(area / "runs", "pending-budget", 1, definition());
        const auto metadata = recent(store);
        require(metadata.at("blocked") == true && metadata.at("occupancy_known") == true &&
            metadata.at("incomplete_files") == 34 && metadata.at("incomplete_bytes") == 136ULL * 1024 * 1024 &&
            metadata.at("bytes") == 136ULL * 1024 * 1024 && !store.save_recent_frame(frame(), true),
            "RECENT_TEST_PREEXISTING_TEMP_BUDGET_IGNORED");
        check_process_restart(area); store.finish_recent_frames();
        require(occupancy(directory) == before, "RECENT_TEST_TEMP_RESTART_GREW_DIRECTORY");
        report.push_back({{"case", "preexisting-136MiB-temps-metered-blocked-restarted"}, {"summary", metadata}});
    }
    {
        const auto area = root / "identity", directory = area / "recent-frames";
        storage::RunStore store(area / "runs", "identity", 1, definition());
        require(enqueue(store, frame()), "RECENT_TEST_IDENTITY_BASELINE_NOT_QUEUED"); wait_saved(store, 1);
        const auto retained = area / "retained-recent-frames";
        std::filesystem::rename(directory, retained); prefill(directory, 1, 64);
        const auto replacement_hash = platform::file_sha256(old_name(directory, 0));
        require(!store.revalidate_recent_frames() && !store.save_recent_frame(frame(2), true) &&
            recent(store).at("blocked_reason") == "RECENT_FRAME_DIRECTORY_IDENTITY_CHANGED" &&
            platform::file_sha256(old_name(directory, 0)) == replacement_hash && occupancy(retained).first == 1,
            "RECENT_TEST_DIRECTORY_REPLACEMENT_NOT_PROTECTED");
        store.finish_recent_frames(); report.push_back({{"case", "directory-identity-replacement-preserved"}, {"summary", recent(store)}});
    }
    std::ofstream(root / "receipt.json") << J{{"device_actions", false}, {"complete", true},
        {"platform", "Windows actual RunStore/file deletion-sharing locks and atomic write faults"}, {"cases", report}}.dump(2);
    return 0;
}
} // namespace recent_frames_contract
