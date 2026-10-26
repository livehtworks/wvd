#include "native_publisher.hpp"
#include "games/wvd/vision/native_asset_resolver.hpp"
#include "games/wvd/vision/builtin_probes.hpp"
#include "games/wvd/vision/combat_phase_probes.hpp"
#include "locale_assets.hpp"
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
#include <limits>
#include <cwctype>

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
    std::filesystem::create_directories(platform::extended_path(path.parent_path()));
    platform::atomic_write(path, std::string(reinterpret_cast<const char *>(bytes), count), false);
}
} // namespace
thread_local std::function<void(const char *)> NativePublication::fault_;
thread_local std::optional<std::uint64_t> NativePublication::available_override_;
void NativePublication::checked_add(std::uint64_t &total, std::uint64_t value) {
    if (value > (std::numeric_limits<std::uint64_t>::max)() - total)
        throw std::runtime_error("NATIVE_PUBLICATION_BUDGET_OVERFLOW");
    total += value;
}
std::uint64_t NativePublication::rounded(std::uint64_t value) {
    if (value > (std::numeric_limits<std::uint64_t>::max)() - 4095)
        throw std::runtime_error("NATIVE_PUBLICATION_BUDGET_OVERFLOW");
    return (value + 4095) & ~std::uint64_t{4095};
}

NativePublication publish_native(const CompiledWorkflow &workflow,
    const recognition::Bundle &baseline, const std::filesystem::path &destination,
    const J &aliases, const J &source_paths, const recognition::Bundle *mod,
    const std::function<void()> &check_cancel, const platform::PreparationObserver &observer,
    const std::function<void(const J &)> &admit_storage) {
    platform::PreparationTimer total_timer("publish_native", observer);
    platform::PreparationTimer input_timer("publisher_inputs", observer);
    if (check_cancel) check_cancel();
    workflow.validate();
    if (!destination.is_absolute() || std::filesystem::exists(platform::extended_path(destination)) ||
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
    std::set<std::string> combat_phases;
    bool chest_phase_collected = false;
    const auto collect = [&](auto &&self, const J &value) -> void {
        if (value.is_object()) {
            if (value.value("mode", "") == "chest_phase" && !chest_phase_collected) {
                chest_phase_collected = true;
                for (const auto &probe : vision::chest_phase_dependencies()) {
                    const auto localized = localize_implicit_probe(probe, workflow.authoring.value("resource_locale", ""));
                    self(self, localized.value_or(probe));
                }
            }
            if (value.value("mode", "") == "combat_phase") {
                const auto phase = value.at("phase").get<std::string>();
                if (combat_phases.insert(phase).second)
                    for (const auto &probe : vision::combat_phase_dependencies(phase)) {
                        const auto localized = localize_implicit_probe(probe, workflow.authoring.value("resource_locale", ""));
                        self(self, localized.value_or(probe));
                    }
            }
            if (value.value("mode", "") == "ocr" || value.value("kind", "") == "ocr")
                languages.insert(recognition::parse_ocr_parameters(value).language);
            for (const auto &probe : vision::implicit_ocr_probes(value.value("mode", "")))
                self(self, probe);
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
    // Admission counts logical aliases separately. The retained content object
    // is counted once only when it is not present; no model bytes are loaded.
    const auto checked_add = &NativePublication::checked_add;
    const auto rounded = &NativePublication::rounded;
    const auto program_text = workflow::serialize(program).dump(2);
    const auto identity_text = identity.dump(2);
    const auto provenance_text = mod ? image_sources.dump(2) : std::string{};
    for (const auto &[relative, hash] : published_manifest) {
        (void)hash;
        auto name = platform::BundleLease::checked_relative(relative).generic_wstring();
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
        if (name == L"publication-layout.json" || name == L"program/flow.json" || name == L"program/identity.json" ||
            (mod && name == L"parameters/image-sources.json")) throw std::runtime_error("NATIVE_PUBLICATION_RESERVED_PATH");
    }
    J resource{{"schema", 1}, {"files", J::array()}};
    std::uint64_t logical_bytes{}, physical_copy_bytes{};
    for (const auto &[relative, hash] : published_manifest) {
        const auto &source = selected_mod_paths.contains(relative) ? *mod_origin : origin;
        const auto size = source.member_size(relative);
        checked_add(logical_bytes, size);
        checked_add(physical_copy_bytes, rounded(size));
        resource["files"].push_back({{"path", relative}, {"sha256", hash}, {"bytes", size}});
    }
    const auto resource_text = resource.dump();
    const auto resource_digest = platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(resource_text.data()), resource_text.size()});
    const auto sets = destination.parent_path() / ".asset-sets";
    const auto shared_root = sets / resource_digest;
    // Pending recovery evidence blocks reuse as well as first materialization.
    platform::OwnedBundleDirectory::require_no_pending(destination.parent_path());
    platform::OwnedBundleDirectory::require_no_pending(destination.parent_path() / "asset-objects");
    platform::OwnedBundleDirectory::require_no_pending(sets);
    platform::OwnedBundleDirectory::require_no_pending(shared_root);
    const bool shared_exists = std::filesystem::exists(platform::extended_path(shared_root));
    const auto layout_text = J{{"schema", 1}, {"kind", "shared_resource_set"},
        {"resource_digest", resource_digest}, {"program_revision", revision}}.dump();
    std::uint64_t required = 2ULL * 1024 * 1024 * 1024;
    const std::uint64_t missing_object_bytes = shared_exists ? 0 : physical_copy_bytes;
    checked_add(required, missing_object_bytes);
    checked_add(required, rounded(program_text.size()));
    checked_add(required, rounded(identity_text.size()));
    checked_add(required, rounded(provenance_text.size()));
    checked_add(required, rounded(layout_text.size()));
    if (!shared_exists) checked_add(required, rounded(resource_text.size()));
    if (published_manifest.size() > ((std::numeric_limits<std::uint64_t>::max)() / 4096) - 32)
        throw std::runtime_error("NATIVE_PUBLICATION_BUDGET_OVERFLOW");
    std::uint64_t overhead = (published_manifest.size() + 32) * 4096ULL;
    checked_add(overhead, 65536);
    checked_add(required, overhead);
    if (admit_storage) admit_storage({{"resource_digest", resource_digest}, {"resource_bytes", logical_bytes},
        {"resource_new", !shared_exists}, {"metadata_bytes", program_text.size() + identity_text.size() + provenance_text.size() + layout_text.size()},
        {"resource_manifest_bytes", resource_text.size()}, {"overhead_bytes", overhead}});
    auto volume = destination.parent_path();
    while (!std::filesystem::exists(platform::extended_path(volume)) && volume != volume.parent_path()) volume = volume.parent_path();
    std::error_code space_error;
    const auto space = std::filesystem::space(platform::extended_path(volume), space_error);
    const auto available = NativePublication::available_override_.value_or(space.available);
    if (space_error || available == (std::numeric_limits<std::uint64_t>::max)())
        throw std::runtime_error("NATIVE_PUBLICATION_SPACE_UNKNOWN");
    if (available < required) throw std::runtime_error("NATIVE_PUBLICATION_SPACE_LOW");
    preparation["space_admission"] = {{"required_bytes", required}, {"available_bytes", available},
        {"logical_copy_bytes", logical_bytes}, {"missing_object_bytes", missing_object_bytes},
        {"reserve_bytes", 2ULL * 1024 * 1024 * 1024}, {"is_reservation", false}};
    if (NativePublication::fault_) NativePublication::fault_("after_space_admission");
    platform::OwnedBundleDirectory::require_no_pending(destination.parent_path());
    platform::OwnedBundleDirectory::require_no_pending(destination.parent_path() / "asset-objects");
    std::filesystem::create_directories(platform::extended_path(destination.parent_path()));
    const auto staging = destination.parent_path() /
        (destination.filename().wstring() + L".staging-" + platform::path_from_utf8(platform::unique_id()).wstring());
    auto owned = platform::OwnedBundleDirectory::create(staging);
    const auto ownership_receipt = owned->receipt();
    recognition::Bundle bundle;
    try {
    if (NativePublication::fault_) NativePublication::fault_("before_bundle_construction");
    bundle = recognition::Bundle{staging, revision, {}};
    platform::PreparationTimer copy_timer("file_copy", observer);
    platform::BundleLease::Manifest shared_manifest{{"manifest.json", platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(resource_text.data()), resource_text.size()})}};
    for (const auto &[relative, hash] : published_manifest) shared_manifest.emplace("payload/" + relative, hash);
    if (!shared_exists) {
        std::filesystem::create_directories(platform::extended_path(sets));
        const auto temporary = sets / (resource_digest + ".staging-" + platform::unique_id());
        auto shared_owner = platform::OwnedBundleDirectory::create(temporary);
        try {
            for (const auto &[relative, hash] : published_manifest) {
                (void)hash;
                if (check_cancel) check_cancel();
                const auto path = temporary / "payload" / platform::BundleLease::checked_relative(relative);
                std::filesystem::create_directories(platform::extended_path(path.parent_path()));
                const auto &source = selected_mod_paths.contains(relative) ? *mod_origin : origin;
                source.copy_member(relative, path, check_cancel);
            }
            write_bytes(temporary, "manifest.json", reinterpret_cast<const std::uint8_t *>(resource_text.data()), resource_text.size());
            { platform::BundleLease verified(temporary, resource_digest, shared_manifest, check_cancel); }
            shared_owner->rename_to(shared_root);
            shared_owner->accept();
            shared_owner->receipt()->require_clean_acceptance();
        } catch (...) { shared_owner->rethrow_with_cleanup(std::current_exception()); }
    }
    // Validate the entire retained set on every hit, not merely its directory name.
    auto shared = std::make_shared<platform::BundleLease>(shared_root, resource_digest, shared_manifest, check_cancel);
    for (const auto &[relative, hash] : published_manifest) bundle.files.push_back({relative, hash});
    preparation["resource_digest"] = resource_digest;
    preparation["resource_bytes"] = logical_bytes;
    preparation["resource_reused"] = shared_exists;
    if (NativePublication::fault_) NativePublication::fault_("after_resource_set");
    preparation["file_copy"] = copy_timer.sample();
    preparation["source_model_bytes_after_copy"] =
        origin.storage_stats().model_bytes +
        (mod_origin ? mod_origin->storage_stats().model_bytes : 0);
    preparation["source_bytes"] = origin.hash_bytes() + (mod_origin ? mod_origin->hash_bytes() : 0);
    preparation["copy_buffer_bytes"] = 65536;
    write_bytes(staging, "program/flow.json",
                reinterpret_cast<const std::uint8_t *>(program_text.data()), program_text.size());
    bundle.files.push_back(
        {"program/flow.json", platform::file_sha256(staging / "program" / "flow.json")});
    write_bytes(staging, "program/identity.json",
                reinterpret_cast<const std::uint8_t *>(identity_text.data()), identity_text.size());
    bundle.files.push_back(
        {"program/identity.json", platform::file_sha256(staging / "program" / "identity.json")});
    if (mod) {
        write_bytes(staging, "parameters/image-sources.json",
                    reinterpret_cast<const std::uint8_t *>(provenance_text.data()), provenance_text.size());
        bundle.files.push_back(
            {"parameters/image-sources.json",
             platform::file_sha256(staging / "parameters" / "image-sources.json")});
    }
    write_bytes(staging, "publication-layout.json", reinterpret_cast<const std::uint8_t *>(layout_text.data()), layout_text.size());
    if (NativePublication::fault_) NativePublication::fault_("after_layout_marker");
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
    owned->rename_to(destination);
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
    NativePublication result{std::move(program), std::move(bundle), std::move(identity), std::move(preparation)};
    result.ownership = std::move(owned);
    return result;
    } catch (...) {
        const auto failure = std::current_exception();
        bundle.lease.reset();
        if (owned) owned->rethrow_with_cleanup(failure);
        if (ownership_receipt->cleanup_pending) {
            std::string primary;
            try { std::rethrow_exception(failure); }
            catch (const std::exception &error) { primary = error.what(); }
            catch (...) { primary = "NON_STANDARD_EXCEPTION"; }
            throw platform::DirectoryCleanupPending(primary, ownership_receipt->cleanup_error, ownership_receipt->path);
        }
        std::rethrow_exception(failure);
    }
}
} // namespace wvd::games::tasks
