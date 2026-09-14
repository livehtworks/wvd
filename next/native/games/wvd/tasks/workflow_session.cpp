#include "workflow_session.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "maafw/preflight.hpp"
#include <fstream>

namespace wvd::games::tasks {
using J = nlohmann::json;
runtime::SessionDefinition publish_workflow(const CompiledWorkflow &workflow,
                                            const maafw::Bundle &source,
                                            const runtime::BehaviorRegistry &registry,
                                            const std::filesystem::path &destination,
                                            const J &aliases) {
    workflow.validate();
    if (!destination.is_absolute() || std::filesystem::exists(destination))
        throw std::runtime_error("COMPILE_DESTINATION_EXISTS_OR_INVALID");
    if (!aliases.is_object())
        throw std::runtime_error("COMPILE_ALIASES_INVALID");
    runtime::SessionDefinition session;
    session.entry = workflow.entry;
    session.terminal_node = workflow.terminal;
    session.recognitions = {vision::binding(aliases)};
    // 缺失或不同修订的 binding 在连接前拒绝，不等候 SDK 首次执行才暴露。
    registry.bind_recognitions(session.recognitions);
    platform::BundleLease::Manifest manifest;
    for (const auto &file : source.files) {
        if (file.relative_path.starts_with("pipeline/"))
            throw std::runtime_error("COMPILE_SOURCE_PIPELINE_PRESENT");
        if (!manifest.emplace(file.relative_path, file.sha256).second)
            throw std::runtime_error("BUNDLE_DUPLICATE_FILE");
    }
    for (const auto &image : workflow.images) {
        const auto selected =
            aliases.contains(image) ? aliases.at(image).get<std::string>() : image;
        const auto relative = "image/" + selected;
        platform::BundleLease::checked_relative(relative);
        if (!manifest.contains(relative))
            throw std::runtime_error("COMPILE_IMAGE_NOT_IN_MANIFEST:" + relative);
    }
    // 源封存覆盖读取与复制期，避免先 hash 再无保护读文件的 TOCTOU。
    platform::BundleLease origin(source.root, source.revision, manifest);
    const auto pipeline = workflow.nodes.dump(2);
    J identity{{"source_revision", source.revision},
               {"source_files", manifest},
               {"kind", workflow.kind},
               {"required_actions", workflow.required_actions},
               {"pipeline", workflow.nodes},
               {"aliases", aliases},
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
    for (const auto &[relative, hash] : manifest) {
        const auto &bytes = origin.bytes(relative);
        write(relative, bytes.data(), bytes.size());
    }
    write("pipeline/workflow.json", reinterpret_cast<const std::uint8_t *>(pipeline.data()),
          pipeline.size());
    session.bundle = {destination, revision, source.files};
    session.bundle.snapshot_parent = destination.parent_path() / "active-snapshots";
    session.bundle.files.push_back(
        {"pipeline/workflow.json", platform::file_sha256(destination / "pipeline/workflow.json")});
    maafw::verify_bundle(session.bundle);
    registry.validate(session);
    return session;
}
} // namespace wvd::games::tasks
