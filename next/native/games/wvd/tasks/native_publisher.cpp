#include "native_publisher.hpp"
#include "games/wvd/vision/native_asset_resolver.hpp"
#include "semantic_catalogue.hpp"
#include "ocr_models.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/preparation_timer.hpp"
#include "workflow/serialization.hpp"
#include <fstream>
#include <set>

namespace wvd::games::tasks {
namespace {
using J = nlohmann::json;
platform::BundleLease::Manifest manifest(const recognition::Bundle &bundle) {
    platform::BundleLease::Manifest result;
    for (const auto &file : bundle.files)
        if (!result.emplace(file.relative_path, file.sha256).second)
            throw std::runtime_error("NATIVE_BUNDLE_DUPLICATE_FILE");
    return result;
}
void write_bytes(const std::filesystem::path &root, const std::string &relative,
                 const std::uint8_t *bytes, std::size_t count) {
    const auto path = root / platform::BundleLease::checked_relative(relative);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes), static_cast<std::streamsize>(count));
    output.close();
    if (!output) throw std::runtime_error("NATIVE_BUNDLE_WRITE_FAILED:" + relative);
}
} // namespace

NativePublication publish_native(const CompiledWorkflow &workflow,
    const recognition::Bundle &baseline, const std::filesystem::path &destination,
    const J &aliases, const J &source_paths, const recognition::Bundle *mod,
    const std::function<void()> &check_cancel, const platform::PreparationObserver &observer) {
    platform::PreparationTimer total_timer("publish_native", observer);
    platform::PreparationTimer input_timer("publisher_inputs", observer);
    if (check_cancel) check_cancel();
    workflow.validate();
    if (!destination.is_absolute() || std::filesystem::exists(destination) ||
        !aliases.is_object() || !source_paths.is_object())
        throw std::runtime_error("NATIVE_PUBLICATION_ARGUMENT_INVALID");
    const auto base_manifest = manifest(baseline);
    const auto mod_manifest = mod ? manifest(*mod) : platform::BundleLease::Manifest{};
    if (mod && base_manifest.contains("parameters/image-sources.json"))
        throw std::runtime_error("NATIVE_PUBLICATION_RESERVED_PATH");
    for (const auto &[relative, hash] : mod_manifest) {
        (void)hash;
        if (!relative.starts_with("image/"))
            throw std::runtime_error("NATIVE_MOD_NOT_IMAGE");
    }
    J image_sources = J::object();
    for (const auto &image : workflow.images) {
        const auto selected = vision::resolve_image_source(baseline, aliases, image, mod);
        const bool from_mod = mod && selected.bundle == mod;
        const auto &files = from_mod ? mod_manifest : base_manifest;
        if (!files.contains(selected.relative_path))
            throw std::runtime_error("NATIVE_IMAGE_MISSING:" + selected.relative_path);
        image_sources[image] = {{"source", from_mod ? "mod" : "baseline"},
                                {"path", selected.relative_path},
                                {"sha256", files.at(selected.relative_path)}};
    }
    J all_paths = workflow.authoring.value("source_paths", J::object());
    all_paths.update(source_paths);
    J preparation{{"publisher_inputs", input_timer.sample()}};
    platform::PreparationTimer lowering_timer("compile_native_program", observer);
    auto program = compile_native_program(workflow, all_paths, "pending");
    preparation["compile_native_program"] = lowering_timer.sample();
    platform::PreparationTimer model_timer("model_contract", observer);
    // 发布前验证实际使用的语言模型，防止作者预览能运行而发布包缺模型。
    const auto serialized = workflow::serialize(program);
    const auto models = J::parse(wvd_ocr_models).at("models");
    std::set<std::string> languages;
    const auto collect = [&](auto &&self, const J &value) -> void {
        if (value.is_object()) {
            if (value.value("mode", "") == "ocr" || value.value("kind", "") == "ocr")
                languages.insert(recognition::parse_ocr_parameters(value).language);
            for (const auto &child : value) self(self, child);
        } else if (value.is_array()) for (const auto &child : value) self(self, child);
    };
    collect(collect, serialized);
    for (const auto &language : languages) {
        const auto &model = models.at(language);
        for (const auto &[name, file] : model.at("files").items()) {
            const auto relative = model.at("bundle_directory").get<std::string>() + "/" + name;
            if (!base_manifest.contains(relative) || base_manifest.at(relative) != file.at("sha256").get<std::string>())
                throw std::runtime_error("NATIVE_OCR_MODEL_MISSING_OR_UNLOCKED:" + relative);
        }
    }
    preparation["model_contract"] = model_timer.sample();
    platform::PreparationTimer identity_timer("publication_identity", observer);
    J identity{{"engine_kind", "wvd_native"},
               {"program_schema", workflow::FlowProgram::schema},
               {"source_revision", baseline.revision},
               {"source_files", base_manifest},
               {"mod_revision", mod ? J(mod->revision) : J(nullptr)},
               {"mod_files", mod_manifest},
               {"image_sources", image_sources},
               {"native_semantic_source_sha256", wvd_semantic_source_sha256},
               {"aliases", aliases},
               {"authoring", workflow.authoring},
               {"definition", workflow.kind},
               {"program", workflow::serialize(program)}};
    const auto material = identity.dump();
    const auto revision = platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(material.data()), material.size()});
    program.revision = revision;
    preparation["publication_identity"] = identity_timer.sample();
    auto published_manifest = base_manifest;
    platform::PreparationTimer source_timer("source_validation", observer);
    platform::BundleLease origin(baseline.root, baseline.revision, base_manifest, check_cancel);
    std::unique_ptr<platform::BundleLease> mod_origin;
    if (mod) mod_origin = std::make_unique<platform::BundleLease>(mod->root, mod->revision, mod_manifest, check_cancel);
    preparation["source_validation"] = source_timer.sample();
    std::set<std::string> selected_mod_paths;
    for (const auto &[name, source] : image_sources.items()) {
        (void)name;
        if (source.at("source") == "mod") {
            const auto relative = source.at("path").get<std::string>();
            published_manifest[relative] = mod_manifest.at(relative);
            selected_mod_paths.insert(relative);
        }
    }
    if (check_cancel) check_cancel();
    std::filesystem::create_directories(destination.parent_path());
    const auto staging = destination.parent_path() /
        (destination.filename().wstring() + L".staging-" + platform::path_from_utf8(platform::unique_id()).wstring());
    if (!std::filesystem::create_directory(staging))
        throw std::runtime_error("NATIVE_PUBLICATION_CREATE_FAILED");
    bool renamed = false;
    // Cleanup only this invocation's exclusive directory, never older published
    // revisions. All temporary leases must be destroyed before rename/rollback.
    struct OwnedPublication {
        const std::filesystem::path &staging, &destination;
        bool &renamed;
        BY_HANDLE_FILE_INFORMATION identity{};
        static BY_HANDLE_FILE_INFORMATION inspect(const std::filesystem::path &path) {
            auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("NATIVE_PUBLICATION_IDENTITY_FAILED");
            BY_HANDLE_FILE_INFORMATION info{};
            const bool ok = GetFileInformationByHandle(handle, &info);
            CloseHandle(handle);
            if (!ok || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("NATIVE_PUBLICATION_IDENTITY_FAILED");
            return info;
        }
        void rollback() const {
            const auto &path = renamed ? destination : staging;
            const auto now = inspect(path);
            if (now.dwVolumeSerialNumber != identity.dwVolumeSerialNumber ||
                now.nFileIndexHigh != identity.nFileIndexHigh || now.nFileIndexLow != identity.nFileIndexLow)
                throw std::runtime_error("NATIVE_PUBLICATION_CLEANUP_IDENTITY_CHANGED");
            std::error_code error;
            std::filesystem::remove_all(path, error);
            if (error) throw std::runtime_error("NATIVE_PUBLICATION_CLEANUP_FAILED:" + error.message());
        }
    } owned{staging, destination, renamed, OwnedPublication::inspect(staging)};
    recognition::Bundle bundle{staging, revision, {}};
    try {
    platform::PreparationTimer copy_timer("file_copy", observer);
    for (const auto &[relative, hash] : published_manifest) {
        if (check_cancel) check_cancel();
        const auto path = staging / platform::BundleLease::checked_relative(relative);
        std::filesystem::create_directories(path.parent_path());
        const auto &source = selected_mod_paths.contains(relative) ? *mod_origin : origin;
        source.copy_member(relative, path, check_cancel);
        bundle.files.push_back({relative, hash});
    }
    preparation["file_copy"] = copy_timer.sample();
    preparation["source_model_bytes_after_copy"] =
        origin.storage_stats().model_bytes +
        (mod_origin ? mod_origin->storage_stats().model_bytes : 0);
    preparation["source_bytes"] = origin.hash_bytes() + (mod_origin ? mod_origin->hash_bytes() : 0);
    preparation["copy_buffer_bytes"] = 65536;
    const auto program_text = workflow::serialize(program).dump(2);
    write_bytes(staging, "program/flow.json",
                reinterpret_cast<const std::uint8_t *>(program_text.data()), program_text.size());
    bundle.files.push_back(
        {"program/flow.json", platform::file_sha256(staging / "program" / "flow.json")});
    const auto identity_text = identity.dump(2);
    write_bytes(staging, "program/identity.json",
                reinterpret_cast<const std::uint8_t *>(identity_text.data()), identity_text.size());
    bundle.files.push_back(
        {"program/identity.json", platform::file_sha256(staging / "program" / "identity.json")});
    if (mod) {
        const auto provenance = image_sources.dump(2);
        write_bytes(staging, "parameters/image-sources.json",
                    reinterpret_cast<const std::uint8_t *>(provenance.data()), provenance.size());
        bundle.files.push_back(
            {"parameters/image-sources.json",
             platform::file_sha256(staging / "parameters" / "image-sources.json")});
    }
    preparation["files"] = bundle.files.size();
    if (check_cancel)
        check_cancel();
    platform::PreparationTimer validation_timer("staging_validation", observer);
    {
        platform::BundleLease staged(staging, revision, manifest(bundle), check_cancel);
        staged.verify_members();
    }
    preparation["staging_validation"] = validation_timer.sample();
    if (check_cancel)
        check_cancel();
    // No FILE_SHARE_DELETE relaxation: the staging lease above is gone. This
    // same-volume rename never replaces a concurrently created destination.
    if (!MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("NATIVE_PUBLICATION_RENAME_FAILED");
    renamed = true;
    if (check_cancel)
        check_cancel();
    bundle.root = destination;
    bundle.snapshot_parent = destination.parent_path() / "active-snapshots";
    platform::PreparationTimer final_timer("target_validation", observer);
    bundle.lease = std::make_shared<platform::BundleLease>(destination, revision, manifest(bundle),
                                                           check_cancel);
    bundle.lease->verify_members();
    if (check_cancel)
        check_cancel();
    preparation["target_validation"] = final_timer.sample();
    preparation["publish_native"] = total_timer.sample();
    return {std::move(program), std::move(bundle), std::move(identity), std::move(preparation)};
    } catch (...) {
        const auto failure = std::current_exception();
        bundle.lease.reset();
        owned.rollback();
        std::rethrow_exception(failure);
    }
}
} // namespace wvd::games::tasks
