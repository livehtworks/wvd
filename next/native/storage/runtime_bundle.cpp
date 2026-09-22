#include "runtime_bundle.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/file_digest.hpp"
#include "maafw/buffers.hpp"
#include <fstream>
#include <iostream>

namespace wvd::storage {
namespace {
void require(bool ok, const char *code) {
    if (!ok)
        throw std::runtime_error(code);
}
} // namespace
void validate_bundle_references(const maafw::Bundle &bundle, const nlohmann::json &value) {
    if (value.is_object()) {
        for (const auto &[key, child] : value.items()) {
            require(key != "_wvd_verified_invocation", "INTEGRITY_INVOCATION_RESERVED");
            if (key == "template") {
                auto names = child.is_array() ? child : nlohmann::json::array({child});
                for (const auto &name : names) {
                    require(name.is_string(), "TEMPLATE_REFERENCE_INVALID");
                    auto path = "image/" + name.get<std::string>();
                    // 运行态只接受闭合文件列表，不让 SDK 延迟读取未知目录成员。
                    bundle.lease->require_member(path);
                }
            } else
                validate_bundle_references(bundle, child);
        }
    } else if (value.is_array())
        for (const auto &child : value)
            validate_bundle_references(bundle, child);
}
maafw::Bundle materialize_bundle(const maafw::Bundle &source) {
    require(!source.lease, "BUNDLE_ALREADY_MATERIALIZED");
    auto parent = source.snapshot_parent;
    if (parent.empty()) {
        // 已有离线 API 的隔离资源都在 .local 下。作者 pack 必须显式指定私有物化目录。
        bool isolated = false;
        for (const auto &part : source.root)
            if (part == ".local")
                isolated = true;
        require(isolated, "RUNTIME_BUNDLE_DESTINATION_REQUIRED");
        parent = source.root.parent_path() / "run-resources";
    }
    platform::BundleLease::Manifest manifest;
    for (const auto &f : source.files)
        require(manifest.emplace(f.relative_path, f.sha256).second, "BUNDLE_DUPLICATE_FILE");
    // 源锁只覆盖复制期，避免复制中目录或文件替换；活动锁与源锁没有共享文件对象/硬链接。
    platform::BundleLease origin(source.root, source.revision, manifest);
    auto author_identity =
        maafw::utf8(std::filesystem::canonical(source.root)) + ":" + source.revision;
    auto identity_hash = platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(author_identity.data()), author_identity.size()});
    auto index_directory = std::filesystem::absolute(parent) / "revisions";
    std::filesystem::create_directories(index_directory);
    auto index = index_directory / (identity_hash + ".json");
    auto expected = nlohmann::json(manifest).dump();
    auto compare_index = [&] {
        std::ifstream input(index, std::ios::binary);
        std::string stored((std::istreambuf_iterator<char>(input)), {});
        require(stored == expected, "BUNDLE_REVISION_REUSED");
    };
    if (std::filesystem::exists(index))
        compare_index();
    else {
        try {
            platform::atomic_write(index, expected, false);
        } catch (...) {
            if (!std::filesystem::is_regular_file(index))
                throw;
            compare_index();
        }
    }
    auto root = std::filesystem::absolute(parent) / platform::unique_id();
    require(std::filesystem::create_directories(root), "RUNTIME_BUNDLE_DIRECTORY_EXISTS");
    // root 是此调用创建的随机快照目录，不是源目录。失败或最后一个lease结束时才回收。
    // shared owner必须先于lease声明，异常展开会先关原生/文件句柄再释放目录。
    const auto directory = std::shared_ptr<const std::filesystem::path>(
        new std::filesystem::path(root), [](const std::filesystem::path *created) noexcept {
            try {
                std::error_code error;
                std::filesystem::remove_all(*created, error);
                if (error) std::clog << "RUNTIME_SNAPSHOT_CLEANUP_FAILED: " << error.message() << '\n';
            } catch (...) { /* 析构不得抛异常或伪报业务完成。 */ }
            delete created;
        });
    for (const auto &[relative, hash] : manifest) {
        auto path = root / platform::BundleLease::checked_relative(relative);
        std::filesystem::create_directories(path.parent_path());
        const auto &content = origin.bytes(relative);
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(content.data()), content.size());
        output.close();
        require(bool(output), "RUNTIME_BUNDLE_COPY_FAILED");
    }
    auto lease = std::shared_ptr<platform::BundleLease>(
        new platform::BundleLease(root, source.revision, manifest),
        [directory](platform::BundleLease *active) { delete active; });
    maafw::Bundle result{root, source.revision, source.files, parent, std::move(lease)};
    for (const auto &[relative, hash] : manifest) {
        if (relative.starts_with("pipeline/") && relative.ends_with(".json")) {
            const auto &bytes = result.lease->bytes(relative);
            auto pipeline = nlohmann::json::parse(bytes.begin(), bytes.end());
            validate_bundle_references(result, pipeline);
        }
    }
    return result;
}
} // namespace wvd::storage
