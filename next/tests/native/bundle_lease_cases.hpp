#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace wvd::tests::bundle_lease_cases {
inline void expect(bool passed, const char *reason) {
    if (!passed) throw std::runtime_error(reason);
}
template <class Action>
void expect_error(Action action, const char *code) {
    try { action(); }
    catch (const std::runtime_error &error) {
        if (std::string(error.what()) == code) return;
        throw;
    }
    throw std::runtime_error(std::string("EXPECTED_ERROR:") + code);
}
struct Directory {
    std::filesystem::path path;
    Directory() : path(std::filesystem::temp_directory_path() /
        ("wvd-bundle-lease-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(GetTickCount64()))) {
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("BUNDLE_TEST_DIRECTORY_EXISTS");
    }
    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
inline void write(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("BUNDLE_TEST_WRITE_FAILED");
    if (!bytes.empty()) file.write(reinterpret_cast<const char *>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) throw std::runtime_error("BUNDLE_TEST_WRITE_FAILED");
}
inline void check() {
    namespace fs = std::filesystem;
    using platform::BundleLease;
    Directory directory;
    const auto root = directory.path / "normal";
    const std::string model_name = "model/source.onnx";
    const auto model_path = root / model_name;
    std::vector<std::uint8_t> model(3 * 65536 + 17);
    for (std::size_t i = 0; i < model.size(); ++i)
        model[i] = static_cast<std::uint8_t>((i * 31 + 7) & 255);
    // Independent SHA256 fixture, including three full chunks and a short tail.
    const std::string model_hash = "b7c0a216f49afa7b185777f1e7a19a998dd1457f765df0f03a90090f8a8d9f80";
    const std::vector<std::uint8_t> image{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    const std::vector<std::uint8_t> json{'{', '"', 'v', '"', ':', '1', '}'};
    write(model_path, model);
    write(root / "model/empty.onnx", {});
    write(root / "image/target.png", image);
    write(root / "definition.json", json);
    const std::string empty_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    BundleLease::Manifest manifest{{model_name, model_hash}, {"model/empty.onnx", empty_hash},
        {"image/target.png", platform::bytes_sha256(image)},
        {"definition.json", platform::bytes_sha256(json)}};
    expect(platform::file_sha256(model_path) == model_hash, "BUNDLE_TEST_MODEL_FIXTURE_INVALID");
    const auto eager_bytes = image.size() + json.size();
    {
        BundleLease lease(root, "bundle-model-test", manifest);
        const auto before = lease.storage_stats();
        expect(before.model_bytes == 0 && before.size_bytes == eager_bytes &&
            before.image_bytes == image.size() && before.json_bytes == json.size(),
            "MODEL_SOURCE_RETAINED_DURING_FREEZE");
        expect(lease.hash_bytes() == eager_bytes + model.size() && lease.file_count() == 4 &&
            lease.hash(model_name) == model_hash, "STREAMED_MODEL_ACCOUNTING_CHANGED");
        expect(lease.bytes("image/target.png") == image && lease.bytes("definition.json") == json,
            "NON_MODEL_BYTES_CHANGED");
        expect(lease.bytes("model/empty.onnx").empty(), "EMPTY_MODEL_BYTES_CHANGED");
        const auto *empty = &lease.bytes("model/empty.onnx");
        expect(&lease.bytes("model/empty.onnx") == empty, "EMPTY_MODEL_REFERENCE_CHANGED");
        expect_error([&] { (void)lease.bytes("model/not-frozen.onnx"); }, "RESOURCE_NOT_IN_MANIFEST");

        // The original file remains readable by the OCR loader, while writes,
        // deletion/replacement and path renaming remain denied for the lease.
        Handle reader{CreateFileW(model_path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        expect(reader.value != INVALID_HANDLE_VALUE, "LOCKED_MODEL_NOT_READABLE");
        BY_HANDLE_FILE_INFORMATION identity{}, after{};
        expect(GetFileInformationByHandle(reader.value, &identity), "MODEL_IDENTITY_UNREADABLE");
        LARGE_INTEGER offset{};
        offset.QuadPart = 13;
        expect(SetFilePointerEx(reader.value, offset, nullptr, FILE_BEGIN), "MODEL_TEST_SEEK_FAILED");
        expect(platform::handle_sha256(reader.value, model.size()) == model_hash &&
            platform::handle_sha256(reader.value, model.size()) == model_hash,
            "HANDLE_HASH_DID_NOT_REWIND");
        expect_error([&] { (void)platform::handle_sha256(reader.value, model.size() + 1); },
            "INTEGRITY_READ_FAILED");
        expect(platform::handle_sha256(reader.value, model.size()) == model_hash,
            "HANDLE_HASH_FAILED_RETRY_BROKEN");
        Handle writer{CreateFileW(model_path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        expect(writer.value == INVALID_HANDLE_VALUE, "MODEL_WRITE_LOCK_DROPPED");
        expect(!MoveFileExW(model_path.c_str(), (root / "model/renamed.onnx").c_str(), 0),
            "MODEL_RENAME_LOCK_DROPPED");

        std::promise<void> release;
        auto start = release.get_future().share();
        std::vector<std::future<const std::vector<std::uint8_t> *>> readers;
        readers.reserve(8); // Allocate before any reader can wait on the start gate.
        std::future<void> observer;
        // If a thread launch fails, unblock already launched readers before any
        // future destructor waits for them during stack unwinding.
        struct StartGuard {
            std::promise<void> &release;
            bool opened{};
            void open() { release.set_value(); opened = true; }
            ~StartGuard() { if (!opened) try { release.set_value(); } catch (...) {} }
        } start_guard{release};
        for (int i = 0; i < 8; ++i)
            readers.push_back(std::async(std::launch::async, [&] {
                start.wait();
                const auto &loaded = lease.bytes(model_name);
                expect(loaded == model, "LAZY_MODEL_BYTES_CHANGED");
                return &loaded;
            }));
        observer = std::async(std::launch::async, [&] {
            start.wait();
            for (int i = 0; i < 128; ++i) {
                const auto stats = lease.storage_stats();
                expect((stats.model_bytes == 0 || stats.model_bytes == model.size()) &&
                    stats.size_bytes == eager_bytes + stats.model_bytes &&
                    stats.capacity_bytes >= stats.size_bytes, "LAZY_MODEL_STATS_TORN");
                std::this_thread::yield();
            }
        });
        start_guard.open();
        const auto *first = readers.front().get();
        for (std::size_t i = 1; i < readers.size(); ++i)
            expect(readers[i].get() == first, "LAZY_MODEL_LOADED_MORE_THAN_ONCE");
        observer.get();
        expect(&lease.bytes(model_name) == first && *first == model, "MODEL_REFERENCE_INVALIDATED");
        const auto loaded = lease.storage_stats();
        expect(loaded.model_bytes == model.size() && loaded.size_bytes == eager_bytes + model.size() &&
            lease.hash_bytes() == eager_bytes + model.size(), "LAZY_MODEL_ACCOUNTING_CHANGED");
        lease.verify_members();
        expect(GetFileInformationByHandle(reader.value, &after) &&
            identity.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
            identity.nFileIndexHigh == after.nFileIndexHigh && identity.nFileIndexLow == after.nFileIndexLow,
            "LOCKED_MODEL_IDENTITY_CHANGED");
    }
    {
        Handle writable{CreateFileW(model_path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        expect(writable.value != INVALID_HANDLE_VALUE, "MODEL_LOCK_NOT_RELEASED");
    }
    auto bad_manifest = manifest;
    bad_manifest[model_name] = std::string(64, '0');
    expect_error([&] { BundleLease rejected(root, "bad-model-hash", bad_manifest); }, "RESOURCE_HASH_MISMATCH");
    // Failed streamed verification must release the handles too.
    {
        Handle writable{CreateFileW(model_path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        expect(writable.value != INVALID_HANDLE_VALUE, "FAILED_MODEL_FREEZE_KEPT_LOCKS");
    }
    auto incomplete = manifest;
    incomplete.erase(model_name);
    expect_error([&] { BundleLease rejected(root, "missing-model", incomplete); }, "RESOURCE_NOT_IN_MANIFEST");

    // Keep the existing inclusive 256 MiB limit. No matching giant vector is
    // allocated by this check; both hash passes use 64 KiB buffers.
    const auto limit_root = directory.path / "limit";
    const auto limit_file = limit_root / "limit.onnx";
    write(limit_file, {});
    constexpr std::uint64_t limit = 256ULL * 1024 * 1024;
    fs::resize_file(limit_file, limit);
    const auto limit_hash = platform::file_sha256(limit_file);
    {
        BundleLease boundary(limit_root, "inclusive-limit", {{"limit.onnx", limit_hash}});
        expect(boundary.hash_bytes() == limit && boundary.storage_stats().size_bytes == 0 &&
            boundary.storage_stats().capacity_bytes == 0, "MODEL_SIZE_LIMIT_OR_RETENTION_CHANGED");
    }
    fs::resize_file(limit_file, limit + 1);
    expect_error([&] { BundleLease rejected(limit_root, "over-limit", {{"limit.onnx", limit_hash}}); },
        "INTEGRITY_FILE_TOO_LARGE");
}
} // namespace wvd::tests::bundle_lease_cases
