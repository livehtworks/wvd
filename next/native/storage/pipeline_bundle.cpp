#include "pipeline_bundle.hpp"
#include "runtime_bundle.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "maafw/buffers.hpp"
#include <fstream>
#include <map>

namespace wvd::storage {
namespace {
using J = nlohmann::json;
using Manifest = platform::BundleLease::Manifest;
void require(bool condition, const char *reason) {
    if (!condition) throw std::runtime_error(reason);
}
std::string hash(const std::string &text) {
    return platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}
void expand(J &value, const platform::BundleLease &source, J &expansions, unsigned depth = 0) {
    require(depth <= 64, "PIPELINE_JSON_DEPTH_INVALID");
    if (value.is_array()) {
        for (auto &child : value) expand(child, source, expansions, depth + 1);
        return;
    }
    if (!value.is_object()) return;
    for (auto &[key, child] : value.items()) {
        require(key != "_wvd_verified_invocation", "INTEGRITY_INVOCATION_RESERVED");
        if (key != "template") {
            expand(child, source, expansions, depth + 1);
            continue;
        }
        auto names = child.is_array() ? child : J::array({child});
        require(!names.empty(), "TEMPLATE_REFERENCE_INVALID");
        J files = J::array();
        bool changed = false;
        for (const auto &name : names) {
            require(name.is_string(), "TEMPLATE_REFERENCE_INVALID");
            const auto relative = "image/" + name.get<std::string>();
            const auto path = source.root() / platform::BundleLease::checked_relative(relative);
            if (!std::filesystem::is_directory(path)) {
                source.require_member(relative);
                files.push_back(name);
                continue;
            }
            changed = true;
            J members = J::array();
            // 对齐固定Maa5.13的递归枚举顺序，不排序或去重；阈值数组保持原样。
            // 读到的每项必须属于源lease，目录新增/替换由末尾verify_members拒绝。
            for (const auto &entry : std::filesystem::recursive_directory_iterator(path)) {
                if (!entry.is_regular_file()) continue;
                const auto generic = entry.path().lexically_relative(source.root()).generic_u8string();
                const std::string member(generic.begin(), generic.end());
                source.require_member(member);
                // 固定C ABI要求可写数据指针；复制单张编码数据，不把源lease暴露为可写。
                auto bytes = source.bytes(member);
                auto image = maafw::image_buffer();
                require(!bytes.empty() && MaaImageBufferSetEncoded(image.get(), bytes.data(), bytes.size()) &&
                    !MaaImageBufferIsEmpty(image.get()) && MaaImageBufferWidth(image.get()) > 0 &&
                    MaaImageBufferHeight(image.get()) > 0, "TEMPLATE_DIRECTORY_IMAGE_INVALID");
                require(member.starts_with("image/"), "RESOURCE_PATH_ESCAPE");
                members.push_back(member.substr(6));
                files.push_back(member.substr(6));
                require(files.size() <= 4096, "TEMPLATE_DIRECTORY_CAPACITY");
            }
            require(!members.empty(), "TEMPLATE_DIRECTORY_EMPTY");
            expansions[name.get<std::string>()] = std::move(members);
        }
        if (changed) child = std::move(files);
    }
}
}
maafw::Bundle prepare_pipeline_bundle(const maafw::Bundle &source,
                                      const std::filesystem::path &destination) {
    require(!source.lease, "BUNDLE_ALREADY_MATERIALIZED");
    require(destination.is_absolute() && !std::filesystem::exists(destination), "PIPELINE_DESTINATION_EXISTS_OR_INVALID");
    // 新目录也不能建在作者包里。用已存在祖先的文件身份检查，涵盖Windows大小写/路径别名。
    for (auto parent = std::filesystem::weakly_canonical(destination).parent_path(); !parent.empty();) {
        if (std::filesystem::exists(parent))
            require(!std::filesystem::equivalent(parent, source.root), "PIPELINE_DESTINATION_INSIDE_SOURCE");
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    Manifest manifest;
    for (const auto &file : source.files)
        require(manifest.emplace(file.relative_path, file.sha256).second, "BUNDLE_DUPLICATE_FILE");
    require(!manifest.contains("parameters/template-expansion.json"), "PIPELINE_RESERVED_PATH_CONFLICT");
    platform::BundleLease origin(source.root, source.revision, manifest);
    std::map<std::string, std::string> pipelines;
    J expansions = J::object();
    for (const auto &[relative, digest] : manifest) {
        if (!relative.starts_with("pipeline/") || !relative.ends_with(".json")) continue;
        const auto &bytes = origin.bytes(relative);
        auto pipeline = J::parse(bytes.begin(), bytes.end());
        expand(pipeline, origin, expansions);
        pipelines.emplace(relative, pipeline.dump(2));
    }
    require(!pipelines.empty(), "PIPELINE_SOURCE_EMPTY");
    origin.verify_members();
    J provenance{{"schema", 1}, {"source_revision", source.revision}, {"source_files", manifest},
        {"directory_templates", expansions}, {"order", "source_recursive_directory_iterator"},
        {"sdk_version", "5.13.0"}};
    Manifest published = manifest;
    for (const auto &[relative, text] : pipelines) published[relative] = hash(text);
    const auto evidence = provenance.dump(2);
    published["parameters/template-expansion.json"] = hash(evidence);
    const auto revision = hash(J{{"source_revision", source.revision}, {"files", published}}.dump());
    require(std::filesystem::create_directories(destination), "PIPELINE_DESTINATION_EXISTS_OR_INVALID");
    auto write = [&](const std::string &relative, const std::uint8_t *data, std::size_t length) {
        const auto file = destination / platform::BundleLease::checked_relative(relative);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(data), length);
        stream.close();
        require(bool(stream), "PIPELINE_BUNDLE_WRITE_FAILED");
    };
    for (const auto &[relative, digest] : manifest) {
        if (const auto found = pipelines.find(relative); found != pipelines.end())
            write(relative, reinterpret_cast<const std::uint8_t *>(found->second.data()), found->second.size());
        else {
            const auto &bytes = origin.bytes(relative);
            write(relative, bytes.data(), bytes.size());
        }
    }
    write("parameters/template-expansion.json", reinterpret_cast<const std::uint8_t *>(evidence.data()), evidence.size());
    origin.verify_members();
    maafw::Bundle result{destination, revision, {}, destination.parent_path() / "active-snapshots"};
    for (const auto &[relative, digest] : published) result.files.push_back({relative, digest});
    // 发布结果重新持锁验证闭包；返回的是待冻结定义的作者副本，不泄露活动lease。
    auto lease = std::make_shared<platform::BundleLease>(destination, revision, published);
    auto checked = result;
    checked.lease = lease;
    for (const auto &[relative, text] : pipelines) validate_bundle_references(checked, J::parse(text));
    return result;
}
}
