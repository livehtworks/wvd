#include "workflow_session.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "games/wvd/vision/asset_resolver.hpp"
#include "games/wvd/diagnostics.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/chest/chest.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "maafw/preflight.hpp"
#include <fstream>
#include <memory>

namespace wvd::games::tasks {
using J = nlohmann::json;
runtime::SessionDefinition publish_workflow(const CompiledWorkflow &workflow,
                                            const maafw::Bundle &source,
                                            const runtime::BehaviorRegistry &registry,
                                            const std::filesystem::path &destination,
                                            const J &aliases, const maafw::Bundle *mod) {
    workflow.validate();
    if (!destination.is_absolute() || std::filesystem::exists(destination))
        throw std::runtime_error("COMPILE_DESTINATION_EXISTS_OR_INVALID");
    if (!aliases.is_object())
        throw std::runtime_error("COMPILE_ALIASES_INVALID");
    runtime::SessionDefinition session;
    session.entry = workflow.entry;
    session.terminal_node = workflow.terminal;
    session.checkpoint_node = workflow.checkpoint;
    session.time_limit = workflow.time_limit;
    session.recognitions = {vision::binding(aliases)};
    const auto dialogue = recovery::dialogue_policy_name(workflow.dialogue_policy);
    if (!dialogue.empty()) session.recognitions.front().parameters["dialogue_task"] = dialogue;
    std::set<std::string> bound_actions;
    for (const auto &node : workflow.nodes) {
        if (node.value("custom_action", "") == "WvdConfirm") {
            if (bound_actions.insert("WvdConfirm").second)
                session.actions.push_back(wvd_confirmation_binding());
        }
        if (node.value("custom_action", "") == "WvdCombat" && bound_actions.insert("WvdCombat").second)
            session.actions.push_back(combat::combat_binding());
        if (node.value("custom_action", "") == "WvdChest" && bound_actions.insert("WvdChest").second)
            session.actions.push_back(chest::chest_binding());
    }
    // 缺失或不同修订的 binding 在连接前拒绝，不等候 SDK 首次执行才暴露。
    registry.bind_recognitions(session.recognitions);
    registry.validate(session);
    platform::BundleLease::Manifest manifest;
    for (const auto &file : source.files) {
        if (file.relative_path.starts_with("pipeline/"))
            throw std::runtime_error("COMPILE_SOURCE_PIPELINE_PRESENT");
        if (!manifest.emplace(file.relative_path, file.sha256).second)
            throw std::runtime_error("BUNDLE_DUPLICATE_FILE");
    }
    platform::BundleLease::Manifest mod_manifest;
    if (mod && manifest.contains("parameters/image-sources.json"))
        throw std::runtime_error("COMPILE_RESERVED_PATH_CONFLICT");
    if (mod)
        for (const auto &file : mod->files) {
            if (!file.relative_path.starts_with("image/"))
                throw std::runtime_error("COMPILE_MOD_NOT_IMAGE");
            if (!mod_manifest.emplace(file.relative_path, file.sha256).second)
                throw std::runtime_error("BUNDLE_DUPLICATE_FILE");
        }
    auto published_manifest = manifest;
    J image_sources = J::object();
    for (const auto &image : workflow.images) {
        const auto selected = vision::resolve_image_source(source, aliases, image, mod);
        const bool from_mod = mod && selected.bundle == mod;
        const auto &files = from_mod ? mod_manifest : manifest;
        if (!files.contains(selected.relative_path))
            throw std::runtime_error("COMPILE_IMAGE_NOT_IN_MANIFEST:" + selected.relative_path);
        const auto &hash = files.at(selected.relative_path);
        published_manifest.emplace(selected.relative_path, hash);
        image_sources[image] = {{"source", from_mod ? "mod" : "baseline"},
                               {"path", selected.relative_path}, {"sha256", hash}};
    }
    // 源封存覆盖读取与复制期，避免先 hash 再无保护读文件的 TOCTOU。
    platform::BundleLease origin(source.root, source.revision, manifest);
    std::unique_ptr<platform::BundleLease> mod_origin;
    if (mod)
        mod_origin = std::make_unique<platform::BundleLease>(mod->root, mod->revision, mod_manifest);
    const auto pipeline = workflow.nodes.dump(2);
    J identity{{"source_revision", source.revision},
               {"source_files", manifest},
               {"mod_revision", mod ? J(mod->revision) : J(nullptr)},
               {"mod_files", mod_manifest},
               {"image_sources", image_sources},
               {"kind", workflow.kind},
               {"time_limit_ms", workflow.time_limit.count()},
               {"required_actions", workflow.required_actions},
               {"pipeline", workflow.nodes},
               {"aliases", aliases},
               {"dialogue_task", dialogue},
               {"registry", registry.manifest()}};
    const auto serialized = identity.dump();
    const auto revision = platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(serialized.data()), serialized.size()});
    if (!std::filesystem::create_directories(destination))
        throw std::runtime_error("COMPILE_DESTINATION_EXISTS_OR_INVALID");
    auto write = [&](const std::string &relative, const std::uint8_t *data, std::size_t size) {
        const auto path = destination / platform::BundleLease::checked_relative(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(data), size);
        output.close();
        if (!output)
            throw std::runtime_error("COMPILE_BUNDLE_WRITE_FAILED");
    };
    for (const auto &[relative, hash] : published_manifest) {
        const auto &bytes = manifest.contains(relative) ? origin.bytes(relative) : mod_origin->bytes(relative);
        write(relative, bytes.data(), bytes.size());
    }
    write("pipeline/workflow.json", reinterpret_cast<const std::uint8_t *>(pipeline.data()),
          pipeline.size());
    session.bundle = {destination, revision, {}};
    for (const auto &[relative, hash] : published_manifest)
        session.bundle.files.push_back({relative, hash});
    // 发布后只读一个完整包，不在识别期间继续读取或扫描用户 mod 目录。
    if (mod) {
        const auto provenance = J{{"schema", 1}, {"baseline_revision", source.revision},
                                  {"mod_revision", mod->revision}, {"images", image_sources}}.dump(2);
        write("parameters/image-sources.json", reinterpret_cast<const std::uint8_t *>(provenance.data()), provenance.size());
        session.bundle.files.push_back({"parameters/image-sources.json",
            platform::file_sha256(destination / "parameters/image-sources.json")});
    }
    session.bundle.snapshot_parent = destination.parent_path() / "active-snapshots";
    session.bundle.files.push_back(
        {"pipeline/workflow.json", platform::file_sha256(destination / "pipeline/workflow.json")});
    maafw::verify_bundle(session.bundle);
    registry.validate(session);
    return session;
}
} // namespace wvd::games::tasks
