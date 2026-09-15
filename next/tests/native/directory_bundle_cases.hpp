#pragma once
#include "runtime_fixture.hpp"
#include "storage/pipeline_bundle.hpp"

namespace fixture {
inline J directory_bundle_case(const maafw::Bundle &source, const J &config,
                               const std::shared_ptr<runtime::BehaviorRegistry> &registry,
                               const std::shared_ptr<OfflineDevice> &device, contracts::InputPolicy policy) {
    J output;
    maafw::Bundle prepared;
    try {
        prepared = storage::prepare_pipeline_bundle(source, source.root.parent_path() / "prepared");
    } catch (const std::exception &e) {
        return {{"preparation_error", e.what()}, {"backend_calls", device->calls.load()}};
    }
    output["source_revision"] = source.revision;
    output["prepared_revision"] = prepared.revision;
    std::ifstream(prepared.root / "pipeline/main.json") >> output["pipeline"];
    std::ifstream(prepared.root / "parameters/template-expansion.json") >> output["expansion"];
    try {
        storage::prepare_pipeline_bundle(source, prepared.root);
    } catch (const std::exception &e) { output["existing_destination_error"] = e.what(); }
    if (config.value("change_author", false)) {
        std::ofstream(source.root / "image/group/target.png", std::ios::binary) << "changed author";
        std::ofstream(source.root / "image/group/new.png", std::ios::binary) << "added author";
        try {
            storage::prepare_pipeline_bundle(source, source.root.parent_path() / "stale-prepared");
        } catch (const std::exception &e) { output["stale_source_error"] = e.what(); }
    }
    // 生成的新revision先进入权限与冻结Run定义，随后才由Gateway物化与执行真实SDK。
    policy.pack_revision = prepared.revision;
    runtime::RunCoordinator coordinator(maafw::path_from_utf8(config.at("run_root")), registry);
    runtime::RunDefinition definition;
    definition.request_id = "directory-pipeline";
    definition.policy = policy;
    definition.initial = {prepared, "Entry", "Terminal", {}, 5000ms, 150ms};
    coordinator.start(definition, device);
    until([&] { auto s = coordinator.snapshot(); return s.quiescent && (s.result_saved || !s.storage_error.empty()); }, 7000ms);
    output["snapshot"] = storage::snapshot_json(coordinator.snapshot());
    output["backend_calls"] = device->calls.load();
    return output;
}
}
