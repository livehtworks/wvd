#pragma once
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include <fstream>
#include <windows.h>

namespace fixture {
// 所有修改均限定在本例新建的副本；不触碰作者资源、活动运行或历史证据。
inline J integrity_lease_case(const maafw::Bundle &bundle, const std::string &scenario,
                              const std::string &invalid_path = {}) {
    platform::BundleLease::Manifest manifest;
    for (const auto &file : bundle.files)
        manifest.emplace(file.relative_path, file.sha256);
    const auto target = bundle.root / "image/target.png";
    const auto before = platform::file_sha256(target);
    HANDLE writer = INVALID_HANDLE_VALUE;
    if (scenario == "writer-held") {
        writer = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(writer != INVALID_HANDLE_VALUE, "FIXTURE_WRITER_NOT_OPENED");
    } else if (scenario == "bad-hash")
        manifest.at("image/target.png") = std::string(64, '0');
    else if (scenario == "bad-manifest")
        manifest.at("image/target.png") = "invalid";
    else if (scenario == "case-collision")
        manifest.emplace("image/Target.png", manifest.at("image/target.png"));
    else if (scenario == "missing-member")
        manifest.emplace("image/zz-missing.png", std::string(64, '0'));
    else if (scenario == "invalid-path")
        manifest.emplace(invalid_path, std::string(64, '0'));

    J result{{"case", scenario}};
    try {
        platform::BundleLease lease(bundle.root, bundle.revision, manifest);
        if (scenario == "directory-rename") {
            std::error_code error;
            std::filesystem::rename(bundle.root / "image", bundle.root / "renamed", error);
            result["rename_blocked"] = bool(error);
            lease.verify_members();
        } else if (scenario == "directory-added") {
            require(std::filesystem::create_directory(bundle.root / "unexpected"), "FIXTURE_DIRECTORY_NOT_CREATED");
            lease.verify_members();
        } else if (scenario == "valid") {
            result["files"] = lease.file_count();
            result["hash_bytes"] = lease.hash_bytes();
            platform::BundleLease reader(bundle.root, bundle.revision, manifest);
            reader.verify_members();
            result["readonly_reader_coexists"] = reader.file_count() == lease.file_count();
        }
    } catch (const std::runtime_error &e) {
        result["error"] = e.what();
    }
    if (writer != INVALID_HANDLE_VALUE)
        CloseHandle(writer);
    // 即使在最后一个成员/哈希处初始化失败，也必须释放此前已获取的所有锁。
    bool released = true;
    for (const auto &file : bundle.files) {
        const auto path = bundle.root / maafw::path_from_utf8(file.relative_path);
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        released = released && handle != INVALID_HANDLE_VALUE;
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    result["all_locks_released"] = released;
    result["target_unchanged"] = platform::file_sha256(target) == before;
    return result;
}
}
