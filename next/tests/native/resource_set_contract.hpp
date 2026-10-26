#pragma once
#include "publication_t0_contract.hpp"
#include "recognition/service.hpp"
#include "ocr_models.hpp"
#include <opencv2/imgproc.hpp>

namespace resource_set_contract {
using namespace publication_t0_contract;
inline int ocr(const fs::path &root, const fs::path &next) {
    check(root.is_absolute() && !exists_os(root), "C08_FRESH_ROOT_REQUIRED");
    mkdirs_os(root);
    recognition::Bundle baseline{root / "source", "locked-two-language-models", {}};
    const auto models = J::parse(wvd_ocr_models).at("models");
    for (const auto &[language, model] : models.items()) {
        (void)language;
        for (const auto &[name, spec] : model.at("files").items()) {
            const auto relative = model.at("bundle_directory").get<std::string>() + "/" + name;
            const auto target = baseline.root / platform::BundleLease::checked_relative(relative);
            const auto source = next / platform::path_from_utf8(spec.at("source").get<std::string>());
            check(platform::file_sha256(source) == spec.at("sha256").get<std::string>(), "C08_OCR_SOURCE_NOT_LOCKED");
            mkdirs_os(target.parent_path()); fs::copy_file(platform::extended_path(source), platform::extended_path(target));
            baseline.files.push_back({relative, spec.at("sha256").get<std::string>()});
        }
    }
    games::tasks::PipelineCompiler compiler("resource-set.ocr"); compiler.route("Entry", {"Terminal"});
    auto p = games::tasks::publish_native(compiler.finish(), baseline, root / "published" / "ocr", J::object());
    J cases = J::array();
    for (const auto &[language, model] : models.items()) {
        for (const auto &[name, spec] : model.at("files").items()) {
            (void)spec;
            const auto logical = model.at("bundle_directory").get<std::string>() + "/" + name;
            const auto path = p.bundle.lease->physical_member(logical);
            const auto writer = CreateFileW(platform::extended_path(path).c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            check(writer == INVALID_HANDLE_VALUE && !MoveFileExW(platform::extended_path(path).c_str(),
                platform::extended_path(path.parent_path() / (name + ".replaced")).c_str(), 0), "C08_OCR_INPUT_UNLOCKED");
        }
        auto input = p.bundle; input.lease.reset(); // Exercise ordinary Service layout loading too.
        recognition::Service service(std::move(input), {});
        cv::Mat image(180, 600, CV_8UC3, cv::Scalar{255,255,255});
        cv::putText(image, "NEXT", {45,125}, cv::FONT_HERSHEY_SIMPLEX, 2.8, cv::Scalar{0,0,0}, 5);
        contracts::FrameEnvelope frame;
        frame.identity.device_id = "isolated"; frame.identity.game_id = "wvd";
        frame.identity.pack_revision = p.bundle.revision; frame.identity.viewport_id = "600x180";
        frame.identity.generation = 1; frame.identity.frame_id = 1;
        frame.identity.raw_size = frame.identity.recognition_size = {600,180};
        frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
        frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(image.data, image.data + image.total() * image.elemSize());
        recognition::OcrParameters parameters; parameters.language = language; parameters.expected_text = {"NEXT"};
        const auto result = service.evaluate(frame, frame.identity, {"ocr", "1", {0,0,600,180}, parameters});
        check(result.outcome != contracts::RecognitionOutcome::Error &&
            (language != "en" || result.outcome == contracts::RecognitionOutcome::Hit), "C08_REAL_OCR_SHARED_PATH_FAILED");
        cases.push_back({{"language", language}, {"inference_completed", true}, {"all_three_inputs_locked", true}});
    }
    p.ownership->accept();
    write(root / "result.json", J{{"passed", true}, {"cases", cases}, {"device_actions", false}}.dump(2));
    std::cout << "PASS C08 Service two-language real OCR and det/rec/keys replacement locks\n";
    return 0;
}
inline int run(const fs::path &root) {
    check(root.is_absolute() && !exists_os(root), "C08_FRESH_ROOT_REQUIRED");
    mkdirs_os(root);
    Reset reset;
    Fixture f(root / "fixture", false);
    // Keep the real publication path, but use a small model for 200 iterations.
    f.model.assign(8193, 'm');
    write(f.source / "models/a.onnx", f.model);
    write(f.source / "models/b.onnx", f.model);
    for (auto &file : f.baseline.files) file.sha256 = platform::file_sha256(f.source / file.relative_path);
    std::string revision, digest;
    FileInfo initial{};
    fs::path physical;
    J cases = J::array();
    std::uint64_t logical = 0, request_bytes = 0;
    for (unsigned i = 0; i < 200; ++i) {
        auto p = f.publish("round-" + std::to_string(i));
        check(p.bundle.root == f.published / ("round-" + std::to_string(i)), "C08_REQUEST_ROOT_CHANGED");
        check(p.bundle.revision == p.program.revision && p.bundle.lease->revision() == p.program.revision,
            "C08_PROGRAM_REVISION_CHANGED");
        if (!i) {
            revision = p.program.revision;
            digest = p.bundle.lease->resource_digest();
            physical = p.bundle.lease->physical_member("models/a.onnx");
            initial = info(physical);
            p.ownership->accept();
            p.ownership->receipt()->require_clean_acceptance();
            // A legacy full tree remains a valid historical publication.
            const auto old = root / "legacy";
            mkdirs_os(old);
            for (const auto &file : p.bundle.files) {
                const auto target = old / platform::BundleLease::checked_relative(file.relative_path);
                mkdirs_os(target.parent_path());
                p.bundle.lease->copy_member(file.relative_path, target);
            }
            auto legacy = platform::BundleLease::open_publication(old);
            check(legacy->revision() == revision && legacy->resource_digest().empty() &&
                legacy->manifest() == p.bundle.lease->manifest(), "C08_LEGACY_HISTORY_CHANGED");
            // copy/link read the same fixed underlying resource handle.
            p.bundle.lease->copy_member("models/a.onnx", root / "copied.onnx");
            p.bundle.lease->link_member("models/a.onnx", root / "linked.onnx");
            check(same_file(info(root / "linked.onnx"), info(physical)) &&
                platform::file_sha256(root / "copied.onnx") == p.bundle.lease->hash("models/a.onnx"),
                "C08_COMPOSITE_COPY_LINK_CHANGED");
            initial = info(physical);
            for (const auto &name : {"models/a.onnx", "models/b.onnx", "program/identity.json"}) {
                const auto path = p.bundle.lease->physical_member(name);
                const auto writer = CreateFileW(platform::extended_path(path).c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
                check(writer == INVALID_HANDLE_VALUE, "C08_MEMBER_WRITE_UNLOCKED");
            }
            const auto manifest_path = f.published / ".asset-sets" / digest / "manifest.json";
            check(!MoveFileExW(platform::extended_path(manifest_path).c_str(),
                platform::extended_path(root / "renamed-manifest.json").c_str(), 0), "C08_MANIFEST_RENAME_UNLOCKED");
            const auto set_root = f.published / ".asset-sets" / digest;
            for (const auto &directory : {f.published, f.published / ".asset-sets", p.bundle.root, set_root}) {
                const auto deletion = CreateFileW(platform::extended_path(directory).c_str(), DELETE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                const auto error = GetLastError();
                if (deletion != INVALID_HANDLE_VALUE) CloseHandle(deletion);
                check(deletion == INVALID_HANDLE_VALUE && error == ERROR_SHARING_VIOLATION,
                    "C08_ANCESTOR_OR_ROOT_DELETE_ACCESS_NOT_BLOCKED");
                check(!MoveFileExW(platform::extended_path(directory).c_str(),
                    platform::extended_path(directory.parent_path() / "rename-probe").c_str(), 0),
                    "C08_ANCESTOR_OR_ROOT_RENAME_NOT_BLOCKED");
            }
            for (const auto &directory : {p.bundle.root, set_root, set_root / "payload"}) {
                const auto writer = CreateFileW(platform::extended_path(directory).c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                const auto error = GetLastError();
                if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
                check(writer == INVALID_HANDLE_VALUE && error == ERROR_SHARING_VIOLATION,
                    "C08_PHYSICAL_ROOT_WRITE_ACCESS_NOT_BLOCKED");
            }
            check(!DeleteFileW(platform::extended_path(manifest_path).c_str()) &&
                !DeleteFileW(platform::extended_path(p.bundle.root / "publication-layout.json").c_str()) &&
                !DeleteFileW(platform::extended_path(physical).c_str()), "C08_CONTROL_OR_PAYLOAD_DELETE_NOT_BLOCKED");
            mkdirs_os(root / "junction-target"); junction(root / "junction", root / "junction-target");
            rejects([&] { p.bundle.lease->copy_member("models/a.onnx", root / "junction/out.onnx"); }, "INTEGRITY_FILE_IDENTITY");
            check(!exists_os(root / "junction-target/out.onnx"), "C08_COMPOSITE_FOLLOWED_REPARSE");
            auto sibling = f.publish("held-sibling");
            sibling.ownership->accept(); sibling.ownership->receipt()->require_clean_acceptance();
            check(same_file(initial, info(sibling.bundle.lease->physical_member("models/a.onnx"))) &&
                info(physical).links == initial.links, "C08_HELD_ANCESTORS_BLOCK_SIBLING_OR_MUTATE_PAYLOAD");
            cases.push_back({{"case", "live_composite_sibling_write_ancestor_delete32_rename_reparse_and_strict_roots"},
                {"passed", true}, {"sibling_probe_requests", 1}});
        }
        check(p.program.revision == revision && p.bundle.lease->resource_digest() == digest &&
            same_file(initial, info(p.bundle.lease->physical_member("models/a.onnx"))) &&
            info(physical).links == initial.links, "C08_RESOURCE_OR_LINKS_GROW_PER_REQUEST");
        for (const auto &file : p.bundle.files) {
            check(platform::file_sha256(p.bundle.lease->physical_member(file.relative_path)) == file.sha256,
                "C08_LOGICAL_HASH_CHANGED");
            logical += p.bundle.lease->member_size(file.relative_path);
        }
        for (const auto &entry : recursive_os(p.bundle.root)) if (entry.is_regular_file()) request_bytes += entry.file_size();
        p.ownership->accept();
        p.ownership->receipt()->require_clean_acceptance();
    }
    std::size_t sets = 0;
    std::uint64_t retained = 0;
    for (const auto &entry : directories_os(f.published / ".asset-sets")) if (entry.is_directory()) ++sets;
    for (const auto &entry : recursive_os(f.published / ".asset-sets")) if (entry.is_regular_file()) retained += entry.file_size();
    check(sets == 1 && logical > retained + request_bytes && info(physical).links == initial.links,
        "C08_RETAINED_NOT_SHARED");
    cases.push_back({{"case", "200_real_publications_one_set_stable_fileid_links_original_revision"},
        {"logical_reference_bytes", logical}, {"resource_set_bytes", retained}, {"request_bytes", request_bytes},
        {"model", info(physical).json()}});
    {
        Fixture selected(root / "mod-selection", false);
        write(selected.source / "image/keep.png", "baseline-member");
        const auto keep_hash = platform::file_sha256(selected.source / "image/keep.png");
        selected.baseline.files.push_back({"image/keep.png", keep_hash});
        recognition::Bundle mod{selected.root / "mod", "mod-v1", {}};
        for (const auto &name : {"keep.png", "selected.png", "unused.png"}) {
            write(mod.root / "image" / name, std::string("mod-byte-member-") + name);
            mod.files.push_back({std::string("image/") + name, platform::file_sha256(mod.root / "image" / name)});
        }
        games::tasks::PipelineCompiler compiler("selected-mod-byte-contract");
        compiler.observe("Entry", games::tasks::PipelineCompiler::image("avatar"), {"Terminal"});
        auto workflow = compiler.finish();
        const J aliases{{"avatar.png", "selected.png"}};
        auto one = games::tasks::publish_native(workflow, selected.baseline, selected.published / "first",
            aliases, J::object(), &mod);
        check(one.bundle.lease->hash("image/keep.png") == keep_hash &&
            !one.bundle.lease->manifest().contains("image/unused.png") &&
            one.identity.at("image_sources").at("avatar.png").at("source") == "mod" &&
            J::parse(one.bundle.lease->bytes("parameters/image-sources.json")) == one.identity.at("image_sources"),
            "C08_SELECTED_MOD_OR_PROVENANCE_CHANGED");
        one.ownership->accept();
        workflow.authoring["resource_locale"] = "zh-Hant";
        auto locale = games::tasks::publish_native(workflow, selected.baseline, selected.published / "locale",
            aliases, J::object(), &mod);
        check(locale.program.revision != one.program.revision &&
            locale.bundle.lease->resource_digest() == one.bundle.lease->resource_digest() &&
            locale.bundle.lease->identity() != one.bundle.lease->identity(), "C08_METADATA_OR_REQUEST_CACHE_IDENTITY_MERGED");
        locale.ownership->accept();
        write(mod.root / "image/selected.png", "changed-avatar-byte-member");
        for (auto &file : mod.files) file.sha256 = platform::file_sha256(mod.root / file.relative_path);
        mod.revision = "mod-v2";
        auto avatar = games::tasks::publish_native(workflow, selected.baseline, selected.published / "avatar",
            aliases, J::object(), &mod);
        check(avatar.bundle.lease->resource_digest() != one.bundle.lease->resource_digest() &&
            avatar.bundle.lease->hash("image/selected.png") != one.bundle.lease->hash("image/selected.png"),
            "C08_CHANGED_AVATAR_NOT_ISOLATED");
        avatar.ownership->accept();
        cases.push_back({{"case", "selected_mod_alias_provenance_locale_and_avatar_versions"}, {"passed", true}});
    }
    {
        // Cross-batch reload uses the same trusted entry point, not source roots.
        auto history = platform::BundleLease::open_publication(f.published / "round-199");
        check(history->resource_digest() == digest && history->revision() == revision &&
            same_file(initial, info(history->physical_member("models/a.onnx"))), "C08_HISTORY_RELOAD_FAILED");
    }
    const auto clone_request = [&](const std::string &name) {
        const auto target = f.published / name;
        for (const auto &entry : recursive_os(f.published / "round-0")) if (entry.is_regular_file()) {
            const auto relative = entry.path().lexically_relative(platform::extended_path(f.published / "round-0"));
            mkdirs_os((target / relative).parent_path());
            fs::copy_file(entry.path(), platform::extended_path(target / relative));
        }
        return target;
    };
    for (const auto &kind : {"unknown-marker", "bad-digest", "wrong-revision", "missing-marker", "extra-request", "extra-empty-directory", "changed-flow"}) {
        const auto request = clone_request(kind);
        const auto marker_path = request / "publication-layout.json";
        if (std::string(kind) == "missing-marker") remove_os(marker_path);
        else if (std::string(kind) == "extra-request") write(request / "extra.txt", "unexpected");
        else if (std::string(kind) == "extra-empty-directory") mkdirs_os(request / "unexpected-empty");
        else if (std::string(kind) == "changed-flow") write(request / "program/flow.json", "{}");
        else {
            std::ifstream input(platform::extended_path(marker_path));
            auto layout = J::parse(input); input.close();
            if (std::string(kind) == "unknown-marker") layout["schema"] = 2;
            if (std::string(kind) == "bad-digest") layout["resource_digest"] = "../../escape";
            if (std::string(kind) == "wrong-revision") layout["program_revision"] = std::string(64, '0');
            write(marker_path, layout.dump());
        }
        bool rejected = false;
        try { auto bad = platform::BundleLease::open_publication(request); }
        catch (const std::exception &) { rejected = true; }
        check(rejected, "C08_MALFORMED_LAYOUT_ACCEPTED");
        cases.push_back({{"case", kind}, {"rejected", true}});
    }
    const auto copy_tree = [&](const fs::path &source, const fs::path &target) {
        for (const auto &entry : recursive_os(source)) if (entry.is_regular_file()) {
            const auto path = target / entry.path().lexically_relative(platform::extended_path(source));
            mkdirs_os(path.parent_path()); fs::copy_file(entry.path(), platform::extended_path(path));
        }
    };
    for (const auto &kind : {"missing-payload", "extra-payload", "changed-payload", "missing-manifest",
                             "path-escape", "case-collision", "two-valid-wrong-set"}) {
        const auto published = root / kind / "published";
        const auto request = published / "request";
        auto set = published / ".asset-sets" / digest;
        copy_tree(f.published / "round-0", request);
        copy_tree(f.published / ".asset-sets" / digest, set);
        if (std::string(kind) == "missing-payload") remove_os(set / "payload/models/a.onnx");
        else if (std::string(kind) == "extra-payload") write(set / "payload/extra", "extra");
        else if (std::string(kind) == "changed-payload") write(set / "payload/models/a.onnx", "changed");
        else if (std::string(kind) == "missing-manifest") remove_os(set / "manifest.json");
        else {
            std::ifstream input(platform::extended_path(set / "manifest.json")); auto resource = J::parse(input); input.close();
            if (std::string(kind) == "path-escape") resource["files"][0]["path"] = "../escape";
            else if (std::string(kind) == "case-collision") {
                auto member = resource["files"][1]; member["path"] = "MODELS/a.onnx";
                resource["files"].push_back(member);
                std::sort(resource["files"].begin(), resource["files"].end(), [](const J &a, const J &b) {
                    return a.at("path").get<std::string>() < b.at("path").get<std::string>();
                });
            } else {
                write(set / "payload/definition.json", "changed");
                resource["files"][0]["sha256"] = platform::file_sha256(set / "payload/definition.json");
                resource["files"][0]["bytes"] = std::uint64_t{7};
            }
            const auto material = resource.dump();
            const auto replacement = platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(material.data()), material.size()});
            write(set / "manifest.json", material);
            fs::rename(platform::extended_path(set), platform::extended_path(set.parent_path() / replacement));
            std::ifstream marker(platform::extended_path(request / "publication-layout.json")); auto layout = J::parse(marker); marker.close();
            layout["resource_digest"] = replacement; write(request / "publication-layout.json", layout.dump());
        }
        bool rejected = false;
        try { auto bad = platform::BundleLease::open_publication(request); } catch (const std::exception &) { rejected = true; }
        check(rejected, "C08_BROKEN_OR_WRONG_SHARED_SET_ACCEPTED");
        cases.push_back({{"case", kind}, {"rejected", true}});
    }
    for (const auto &phase : {"after_resource_set", "after_layout_marker"}) {
        games::tasks::NativePublicationTestAccess::fault([&](const char *at) {
            if (std::string(at) == phase) throw std::runtime_error("C08_PUBLICATION_WINDOW_FAULT");
        });
        rejects([&] { f.publish(phase); }, "C08_PUBLICATION_WINDOW_FAULT");
        games::tasks::NativePublicationTestAccess::fault({});
        check(!exists_os(f.published / phase) && !has_staging(f.published), "C08_FAULT_WINDOW_LEFT_REQUEST");
        auto history = platform::BundleLease::open_publication(f.published / "round-0");
        check(history->revision() == revision && same_file(initial, info(physical)), "C08_FAULT_WINDOW_CHANGED_HISTORY");
        cases.push_back({{"case", phase}, {"rolled_back_request_only", true}});
    }
    {
        auto p = f.publish("reload-reuse"); p.ownership->accept(); p.bundle.lease.reset();
        auto held = platform::OwnedBundleDirectory::create(f.published / ".asset-sets" / "interrupted.staging-owned");
        rejects([&] { f.publish("pending-reuse"); }, "PREPARATION_CLEANUP_PENDING");
        held->rollback();
    }
    cases.push_back({{"case", "reuse_checks_shared_pending"}, {"passed", true}});
    {
        recognition::Bundle reserved{root / "reserved-source", "reserved-name-fixture", {}};
        write(reserved.root / "Publication-Layout.json", "old-ordinary-member");
        reserved.files.push_back({"Publication-Layout.json", platform::file_sha256(reserved.root / "Publication-Layout.json")});
        rejects([&] { games::tasks::publish_native(f.workflow, reserved, root / "reserved-target", J::object()); },
            "[json.exception.parse_error.");
        check(!exists_os(root / "reserved-target") && !has_staging(root) &&
            platform::file_sha256(reserved.root / "Publication-Layout.json") == reserved.files.front().sha256,
            "C08_RESERVED_NAME_SOURCE_CHANGED_OR_MATERIALIZED");
        cases.push_back({{"case", "windows_case_insensitive_marker_is_not_ordinary_payload"}, {"rejected", true}});
        recognition::Bundle generated{root / "reserved-generated-source", "reserved-generated-fixture", {}};
        write(generated.root / "PROGRAM/FLOW.JSON", "ordinary-byte-member");
        generated.files.push_back({"PROGRAM/FLOW.JSON", platform::file_sha256(generated.root / "PROGRAM/FLOW.JSON")});
        rejects([&] { games::tasks::publish_native(f.workflow, generated, root / "reserved-generated-target", J::object()); },
            "NATIVE_PUBLICATION_RESERVED_PATH");
        check(!exists_os(root / "reserved-generated-target") && !has_staging(root) &&
            platform::file_sha256(generated.root / "PROGRAM/FLOW.JSON") == generated.files.front().sha256,
            "C08_RESERVED_GENERATED_NAME_SOURCE_CHANGED_OR_MATERIALIZED");
        cases.push_back({{"case", "windows_case_insensitive_generated_path_conflict"}, {"rejected", true}});
    }
    write(root / "result.json", J{{"passed", true}, {"cases", cases}, {"device_actions", false},
        {"program_revision", revision}, {"resource_digest", digest}, {"rounds", 200}}.dump(2));
    std::cout << "PASS C08 200 publications, immutable composition, legacy history, strict markers and pending reuse\n";
    return 0;
}
}
