#include "native_publisher.hpp"
#include "games/wvd/vision/native_asset_resolver.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/path_utf8.hpp"
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
    const J &aliases, const J &source_paths, const recognition::Bundle *mod) {
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
    auto program = compile_native_program(workflow, source_paths, "pending");
    J identity{{"engine_kind", "wvd_native"},
               {"program_schema", workflow::FlowProgram::schema},
               {"source_revision", baseline.revision},
               {"source_files", base_manifest},
               {"mod_revision", mod ? J(mod->revision) : J(nullptr)},
               {"mod_files", mod_manifest},
               {"image_sources", image_sources},
               {"aliases", aliases},
               {"authoring", workflow.authoring},
               {"definition", workflow.kind},
               {"program", workflow::serialize(program)}};
    const auto material = identity.dump();
    const auto revision = platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(material.data()), material.size()});
    program.revision = revision;
    auto published_manifest = base_manifest;
    platform::BundleLease origin(baseline.root, baseline.revision, base_manifest);
    std::unique_ptr<platform::BundleLease> mod_origin;
    if (mod) mod_origin = std::make_unique<platform::BundleLease>(mod->root, mod->revision, mod_manifest);
    std::set<std::string> selected_mod_paths;
    for (const auto &[name, source] : image_sources.items()) {
        (void)name;
        if (source.at("source") == "mod") {
            const auto relative = source.at("path").get<std::string>();
            published_manifest[relative] = mod_manifest.at(relative);
            selected_mod_paths.insert(relative);
        }
    }
    if (!std::filesystem::create_directories(destination))
        throw std::runtime_error("NATIVE_PUBLICATION_CREATE_FAILED");
    recognition::Bundle bundle{destination, revision, {}};
    for (const auto &[relative, hash] : published_manifest) {
        const auto &bytes = selected_mod_paths.contains(relative)
            ? mod_origin->bytes(relative) : origin.bytes(relative);
        write_bytes(destination, relative, bytes.data(), bytes.size());
        bundle.files.push_back({relative, hash});
    }
    const auto program_text = workflow::serialize(program).dump(2);
    write_bytes(destination, "program/flow.json",
        reinterpret_cast<const std::uint8_t *>(program_text.data()), program_text.size());
    bundle.files.push_back({"program/flow.json",
        platform::file_sha256(destination / "program" / "flow.json")});
    const auto identity_text = identity.dump(2);
    write_bytes(destination, "program/identity.json",
        reinterpret_cast<const std::uint8_t *>(identity_text.data()), identity_text.size());
    bundle.files.push_back({"program/identity.json",
        platform::file_sha256(destination / "program" / "identity.json")});
    if (mod) {
        const auto provenance = image_sources.dump(2);
        write_bytes(destination, "parameters/image-sources.json",
            reinterpret_cast<const std::uint8_t *>(provenance.data()), provenance.size());
        bundle.files.push_back({"parameters/image-sources.json",
            platform::file_sha256(destination / "parameters" / "image-sources.json")});
    }
    bundle.snapshot_parent = destination.parent_path() / "active-snapshots";
    bundle.lease = std::make_shared<platform::BundleLease>(destination, revision, manifest(bundle));
    bundle.lease->verify_members();
    return {std::move(program), std::move(bundle), std::move(identity)};
}
} // namespace wvd::games::tasks
