#include "games/wvd/tasks/native_publisher.hpp"
#include "semantic_catalogue.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/path_utf8.hpp"
#include "workflow/serialization.hpp"
#include <fstream>
#include <iostream>
#include <windows.h>

namespace {
using J = nlohmann::json;
namespace fs = std::filesystem;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action, const std::string &code) {
    try { action(); } catch (const std::exception &error) {
        if (std::string(error.what()).starts_with(code)) return;
        throw;
    }
    throw std::runtime_error("EXPECTED_REJECTION:" + code);
}
void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), text.size());
    output.close();
    require(bool(output), "FIXTURE_WRITE_FAILED");
}
bool staging_exists(const fs::path &root) {
    for (const auto &entry : fs::directory_iterator(root))
        if (entry.path().filename().string().find(".staging-") != std::string::npos) return true;
    return false;
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "NEW_ISOLATED_ROOT_REQUIRED");
        const auto root = fs::absolute(wvd::platform::path_from_utf8(argv[1]));
        require(!fs::exists(root), "TEST_ROOT_ALREADY_EXISTS");
        fs::create_directories(root);
        const auto source = root / "source";
        write(source / "models/large.onnx", std::string(8 * 1024 * 1024 + 17, 'x'));
        write(source / "definition.json", "{\"schema\":1}");
        wvd::recognition::Bundle baseline{source, "known-baseline", {}};
        wvd::platform::BundleLease::Manifest files;
        for (const auto &name : {"models/large.onnx", "definition.json"}) {
            const auto hash = wvd::platform::file_sha256(source / name);
            baseline.files.push_back({name, hash});
            files.emplace(name, hash);
        }
        wvd::games::tasks::PipelineCompiler compiler("publication.test");
        compiler.route("Entry", {"Terminal"});
        auto workflow = compiler.finish();
        const auto pending = wvd::games::tasks::compile_native_program(workflow, J::object(), "pending");
        // Reconstruct the baseline identity contract independently of publisher
        // diagnostics and paths. Neither may enter the revision calculation.
        const J expected_identity{{"engine_kind", "wvd_native"},
            {"program_schema", wvd::workflow::FlowProgram::schema}, {"source_revision", baseline.revision},
            {"source_files", files}, {"mod_revision", nullptr}, {"mod_files", J::object()},
            {"image_sources", J::object()},
            {"native_semantic_source_sha256", wvd_semantic_source_sha256},
            {"aliases", J::object()}, {"authoring", workflow.authoring}, {"definition", workflow.kind},
            {"program", wvd::workflow::serialize(pending)}};
        const auto identity_text = expected_identity.dump();
        const auto expected_revision = wvd::platform::bytes_sha256({
            reinterpret_cast<const std::uint8_t *>(identity_text.data()), identity_text.size()});
        J results = J::array();
        {
            const auto publication = wvd::games::tasks::publish_native(workflow, baseline, root / "normal", J::object());
            require(publication.identity == expected_identity && publication.program.revision == expected_revision,
                    "IDENTITY_OR_REVISION_CHANGED");
            require(publication.preparation.at("source_model_bytes_after_copy") == 0 &&
                    publication.preparation.at("copy_buffer_bytes") == 65536, "MODEL_CACHE_OR_BUFFER_UNBOUNDED");
            require(publication.bundle.lease->storage_stats().model_bytes == 0, "TARGET_MODEL_CACHE_LOADED");
            publication.bundle.lease->verify_members();
            for (const auto &file : publication.bundle.files)
                require(wvd::platform::file_sha256(publication.bundle.root / file.relative_path) == file.sha256,
                        "PUBLISHED_FILE_HASH_MISMATCH");
            require(!staging_exists(root), "STAGING_NOT_RENAMED");
            auto model = publication.bundle.root / "models/large.onnx";
            const auto writer = CreateFileW(model.c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            require(writer == INVALID_HANDLE_VALUE, "TARGET_WRITE_LOCK_RELAXED");
            require(!MoveFileExW(publication.bundle.root.c_str(), (root / "bad-rename").c_str(), 0),
                    "TARGET_DELETE_LOCK_RELAXED");
            results.push_back({{"case", "identity_manifest_streaming_rename_and_lock"}, {"passed", true},
                               {"metrics", publication.preparation}});
        }
        {
            wvd::platform::BundleLease lease(source, baseline.revision, files);
            int calls = 0;
            rejects([&] { lease.copy_member("models/large.onnx", root / "partial.onnx", [&] {
                if (++calls == 5) throw std::runtime_error("PREPARATION_CANCELLED");
            }); }, "PREPARATION_CANCELLED");
            require(fs::file_size(root / "partial.onnx") <= 65536 && lease.storage_stats().model_bytes == 0,
                    "COPY_CANCELLATION_UNBOUNDED");
            lease.copy_member("models/large.onnx", root / "copy.onnx");
            require(wvd::platform::file_sha256(root / "copy.onnx") == files.at("models/large.onnx"),
                    "CANCELLED_COPY_RETRY_CURSOR_BROKEN");
            rejects([&] { lease.copy_member("models/large.onnx", root / "copy.onnx"); }, "NATIVE_BUNDLE_CREATE_FAILED");
            rejects([&] { lease.copy_member("../escape", root / "escape"); }, "RESOURCE_PATH_INVALID");
            results.push_back({{"case", "copy_cancel_rewind_and_no_overwrite"}, {"passed", true}});
        }
        for (const auto &mode : {"before_copy", "during_copy", "before_rename", "after_rename"}) {
            const auto destination = root / mode;
            bool triggered = false;
            bool staged_lock_observed = false;
            rejects([&] { wvd::games::tasks::publish_native(workflow, baseline, destination, J::object(),
                J::object(), nullptr, [&] {
                    if (std::string(mode) == "before_copy") { triggered = true; throw std::runtime_error("PREPARATION_CANCELLED"); }
                    if (std::string(mode) == "after_rename" && fs::exists(destination)) {
                        triggered = true; throw std::runtime_error("PREPARATION_CANCELLED");
                    }
                    for (const auto &entry : fs::directory_iterator(root)) {
                        if (!entry.path().filename().string().starts_with(std::string(mode) + ".staging-")) continue;
                        if (std::string(mode) == "during_copy" && fs::exists(entry.path() / "models/large.onnx") &&
                            fs::file_size(entry.path() / "models/large.onnx") >= 65536) {
                            triggered = true; throw std::runtime_error("PREPARATION_CANCELLED");
                        }
                        if (std::string(mode) == "before_rename" && fs::exists(entry.path() / "program/identity.json")) {
                            const auto handle = CreateFileW(entry.path().c_str(), DELETE, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                            if (handle != INVALID_HANDLE_VALUE) {
                                CloseHandle(handle);
                                if (staged_lock_observed) {
                                    triggered = true; throw std::runtime_error("PREPARATION_CANCELLED");
                                }
                            } else staged_lock_observed = true;
                        }
                    }
                }); }, "PREPARATION_CANCELLED");
            require(triggered && !fs::exists(destination) && !staging_exists(root), "CANCELLED_PUBLICATION_LEFT_OUTPUT");
            require(fs::exists(root / "normal/program/identity.json"), "OLDER_PUBLICATION_REMOVED");
            results.push_back({{"case", std::string("publication_cancel_") + mode}, {"passed", true}});
        }
        {
            auto bad = baseline;
            bad.files.front().sha256 = std::string(64, '0');
            rejects([&] { wvd::games::tasks::publish_native(workflow, bad, root / "bad-hash", J::object()); },
                    "RESOURCE_HASH_MISMATCH");
            require(!fs::exists(root / "bad-hash"), "BAD_HASH_PUBLISHED");
            results.push_back({{"case", "hash_mismatch_rejected"}, {"passed", true}});
        }
        {
            const auto input = CreateFileW((source / "models/large.onnx").c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            require(input != INVALID_HANDLE_VALUE, "RAW_FIXTURE_OPEN_FAILED");
            struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{input};
            rejects([&] { wvd::platform::handle_sha256(input, fs::file_size(source / "models/large.onnx") + 1); },
                    "INTEGRITY_READ_FAILED");
            rejects([&] { wvd::platform::copy_handle_sha256(input, input, 65536); }, "NATIVE_BUNDLE_WRITE_FAILED");
            results.push_back({{"case", "real_short_read_and_readonly_write_failure"}, {"passed", true}});
        }
        write(root / "result.json", J{{"passed", true}, {"cases", results}, {"game_inputs", 0}}.dump(2));
        std::cout << "Publication contract, bounded copy, cancellation, lock/rename and real I/O failures passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
