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
namespace {
void add_bytes(std::uint64_t &total, std::uint64_t value) {
    if (value > (std::numeric_limits<std::uint64_t>::max)() - total)
        throw std::runtime_error("NATIVE_PUBLICATION_BUDGET_OVERFLOW");
    total += value;
}
std::uint64_t allocation(std::uint64_t value) {
    if (value > (std::numeric_limits<std::uint64_t>::max)() - 4095)
        throw std::runtime_error("NATIVE_PUBLICATION_BUDGET_OVERFLOW");
    return (value + 4095) & ~std::uint64_t{4095};
}
std::uint64_t metadata_overhead(std::size_t files) {
    if (files > ((std::numeric_limits<std::uint64_t>::max)() / 4096) - 32)
        throw std::runtime_error("NATIVE_PUBLICATION_BUDGET_OVERFLOW");
    auto overhead = (files + 32) * 4096ULL; add_bytes(overhead,65536); return overhead;
}
J admit_space(std::filesystem::path volume, std::uint64_t missing, const std::vector<std::uint64_t> &metadata,
    std::size_t count, std::uint64_t logical, std::optional<std::uint64_t> available_override,
    std::uint64_t transaction_bytes = 0) {
    std::uint64_t required = 2ULL * 1024 * 1024 * 1024;
    add_bytes(required,missing);
    for (const auto bytes : metadata) add_bytes(required,allocation(bytes));
    add_bytes(required,metadata_overhead(count)); add_bytes(required,allocation(transaction_bytes));
    while (!std::filesystem::exists(platform::extended_path(volume)) && volume != volume.parent_path()) volume = volume.parent_path();
    std::error_code error;
    const auto space = std::filesystem::space(platform::extended_path(volume),error);
    const auto available = available_override.value_or(space.available);
    if (error || available == (std::numeric_limits<std::uint64_t>::max)()) throw std::runtime_error("NATIVE_PUBLICATION_SPACE_UNKNOWN");
    if (available < required) throw std::runtime_error("NATIVE_PUBLICATION_SPACE_LOW");
    return {{"required_bytes",required},{"available_bytes",available},{"logical_copy_bytes",logical},
        {"missing_object_bytes",missing},{"transaction_bytes",transaction_bytes},
        {"reserve_bytes",2ULL*1024*1024*1024},{"is_reservation",false}};
}
std::shared_ptr<platform::BundleLease> materialize_set(const std::filesystem::path &sets,
    const std::string &digest, const std::string &text, const platform::BundleLease::Manifest &resources,
    const std::function<const platform::BundleLease &(const std::string &)> &source,
    const std::function<void()> &check) {
    const auto target = sets / digest;
    platform::OwnedBundleDirectory::require_no_pending(sets);
    platform::BundleLease::Manifest manifest{{"manifest.json", digest}};
    for (const auto &[relative, hash] : resources) manifest.emplace("payload/" + relative, hash);
    if (!std::filesystem::exists(platform::extended_path(target))) {
        std::filesystem::create_directories(platform::extended_path(sets));
        const auto temporary = sets / (digest + ".staging-" + platform::unique_id());
        auto owner = platform::OwnedBundleDirectory::create(temporary);
        try {
            for (const auto &[relative, hash] : resources) {
                (void)hash;
                if (check) check();
                const auto path = temporary / "payload" / platform::BundleLease::checked_relative(relative);
                std::filesystem::create_directories(platform::extended_path(path.parent_path()));
                source(relative).copy_member(relative, path, check);
            }
            write_bytes(temporary, "manifest.json", reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
            { platform::BundleLease verified(temporary, digest, manifest, check); }
            owner->rename_to(target); owner->accept(); owner->receipt()->require_clean_acceptance();
        } catch (...) { owner->rethrow_with_cleanup(std::current_exception()); }
    }
    return std::make_shared<platform::BundleLease>(target, digest, manifest, check);
}
}
J admit_existing_publication(const platform::BundleLease &source, const std::filesystem::path &stage,
    std::uint64_t transaction_bytes) {
    if (!source.resource_digest().empty()) throw std::runtime_error("PUBLICATION_ALREADY_SHARED");
    const auto &resources = source.publication_resources();
    J descriptor{{"schema",1},{"files",J::array()}};
    std::uint64_t logical{}, copies{};
    for (const auto &[relative,hash] : resources) {
        const auto bytes = source.member_size(relative); add_bytes(logical,bytes); add_bytes(copies,allocation(bytes));
        descriptor["files"].push_back({{"path",relative},{"sha256",hash},{"bytes",bytes}});
    }
    const auto text = descriptor.dump();
    const auto digest = platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(text.data()),text.size()});
    const auto sets = stage.parent_path() / ".asset-sets";
    platform::OwnedBundleDirectory::require_no_pending(sets);
    const bool present = std::filesystem::exists(platform::extended_path(sets / digest));
    const auto marker = J{{"schema",1},{"kind","shared_resource_set"},{"resource_digest",digest},{"program_revision",source.revision()}}.dump();
    std::vector<std::uint64_t> metadata{marker.size(),present ? 0 : text.size()};
    for (const auto &[name,hash] : source.manifest()) { (void)hash; if (!resources.contains(name)) metadata.push_back(source.member_size(name)); }
    auto result = admit_space(stage.parent_path(),present ? 0 : copies,metadata,resources.size(),logical,
        NativePublication::available_override_,transaction_bytes);
    result["resource_digest"] = digest; return result;
}
void prepare_existing_publication(const platform::BundleLease &source, const std::filesystem::path &stage) {
    (void)admit_existing_publication(source,stage,0);
    if (!source.resource_digest().empty()) throw std::runtime_error("PUBLICATION_ALREADY_SHARED");
    const auto &resources = source.publication_resources();
    J descriptor{{"schema", 1}, {"files", J::array()}};
    for (const auto &[relative, hash] : resources)
        descriptor["files"].push_back({{"path",relative},{"sha256",hash},{"bytes",source.member_size(relative)}});
    const auto text = descriptor.dump();
    const auto digest = platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
    auto shared = materialize_set(stage.parent_path() / ".asset-sets", digest, text, resources,
        [&](const std::string &) -> const platform::BundleLease & { return source; }, {});
    for (const auto &[relative, hash] : source.manifest()) {
        (void)hash;
        if (resources.contains(relative)) continue;
        const auto target = stage / platform::BundleLease::checked_relative(relative);
        std::filesystem::create_directories(platform::extended_path(target.parent_path()));
        source.copy_member(relative, target);
    }
    const auto marker = J{{"schema",1},{"kind","shared_resource_set"},
        {"resource_digest",digest},{"program_revision",source.revision()}}.dump();
    write_bytes(stage, "publication-layout.json", reinterpret_cast<const std::uint8_t *>(marker.data()), marker.size());
    auto verified = platform::BundleLease::open_publication(stage);
    if (verified->manifest() != source.manifest() || verified->revision() != source.revision())
        throw std::runtime_error("PUBLICATION_CONVERSION_MISMATCH");
}
thread_local std::function<void(const char *)> NativePublication::fault_;
thread_local std::optional<std::uint64_t> NativePublication::available_override_;
void NativePublication::checked_add(std::uint64_t &total, std::uint64_t value) {
    add_bytes(total,value);
}
std::uint64_t NativePublication::rounded(std::uint64_t value) {
    return allocation(value);
}

NativePublication publish_native(const CompiledWorkflow &workflow,
    const recognition::Bundle &baseline, const std::filesystem::path &destination,
    const J &aliases, const J &source_paths, const recognition::Bundle *mod,
    const std::function<void()> &check_cancel, const platform::PreparationObserver &observer,
    const std::function<void(const J &)> &admit_storage,
    const std::function<void(const std::string &)> &prepare_destination) {
    platform::PreparationTimer total_timer("publish_native", observer);
    platform::PreparationTimer input_timer("publisher_inputs", observer);
    if (check_cancel) check_cancel();
    workflow.validate();
    if (!destination.is_absolute() || (!prepare_destination && std::filesystem::exists(platform::extended_path(destination))) ||
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
    if (prepare_destination) {
        if (check_cancel) check_cancel();
        prepare_destination(revision);
        if (std::filesystem::exists(platform::extended_path(destination))) {
            auto lease = platform::BundleLease::open_publication(destination, check_cancel);
            if (lease->revision() != revision) throw std::runtime_error("EXECUTION_PUBLICATION_REVISION_MISMATCH");
            // Verify the same identity/bytes, not a filename or mutable source.
            if (J::parse(lease->bytes("program/identity.json")) != identity)
                throw std::runtime_error("PUBLICATION_IDENTITY_MISMATCH");
            if (admit_storage) admit_storage({{"resource_new", false}, {"metadata_bytes", 0}, {"overhead_bytes", 0}});
            recognition::Bundle bundle{destination, revision, {}};
            for (const auto &[name, hash] : lease->manifest()) bundle.files.push_back({name, hash});
            bundle.lease = std::move(lease);
            preparation["publication_reused"] = true;
            preparation["publication_materialized_bytes"] = 0;
            preparation["publish_native"] = total_timer.sample();
            return {std::move(program), std::move(bundle), std::move(identity), std::move(preparation)};
        }
    }
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
    const std::uint64_t missing_object_bytes = shared_exists ? 0 : physical_copy_bytes;
    const auto overhead = metadata_overhead(published_manifest.size());
    if (admit_storage) admit_storage({{"resource_digest", resource_digest}, {"resource_bytes", logical_bytes},
        {"resource_new", !shared_exists}, {"metadata_bytes", program_text.size() + identity_text.size() + provenance_text.size() + layout_text.size()},
        {"resource_manifest_bytes", resource_text.size()}, {"overhead_bytes", overhead}});
    preparation["space_admission"] = admit_space(destination.parent_path(), missing_object_bytes,
        {program_text.size(),identity_text.size(),provenance_text.size(),layout_text.size(),shared_exists ? 0 : resource_text.size()},
        published_manifest.size(),logical_bytes,NativePublication::available_override_);
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
    auto shared = materialize_set(sets, resource_digest, resource_text, published_manifest,
        [&](const std::string &relative) -> const platform::BundleLease & {
            return selected_mod_paths.contains(relative) ? *mod_origin : origin;
        }, check_cancel);
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
    preparation["publication_reused"] = false;
    preparation["publication_materialized_bytes"] = program_text.size() + identity_text.size() +
        provenance_text.size() + layout_text.size() + (shared_exists ? 0 : logical_bytes + resource_text.size());
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
