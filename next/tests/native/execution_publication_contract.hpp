#pragma once
#include "games/wvd/tasks/execution_publication.hpp"
#include "storage/run_store.hpp"

namespace execution_publication_contract {
using J = nlohmann::json;
namespace fs = std::filesystem;
inline void need(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action, const std::string &prefix) {
    try { action(); } catch (const std::exception &e) {
        if (std::string(e.what()).starts_with(prefix)) return;
        throw;
    }
    throw std::runtime_error("EXPECTED_CACHE_REJECTION:" + prefix);
}
inline void write(const fs::path &path, const J &json) {
    wvd::platform::atomic_write(path, json.dump(), fs::exists(path));
}
inline std::pair<std::size_t, std::uint64_t> size(const fs::path &root) {
    std::pair<std::size_t, std::uint64_t> total{};
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file()) { ++total.first; total.second += entry.file_size(); }
    return total;
}
inline std::array<DWORD, 3> file_id(const fs::path &path) {
    const auto handle = CreateFileW(wvd::platform::extended_path(path).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    need(handle != INVALID_HANDLE_VALUE, "SLOT_TEST_FILE_ID_OPEN_FAILED");
    BY_HANDLE_FILE_INFORMATION info{};
    const auto ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    need(ok != FALSE, "SLOT_TEST_FILE_ID_FAILED");
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
inline J business() {
    J facts = J::object();
    for (const char *pointer : {"/inn_payment_pending", "/karma_pending", "/bounty_report_pending",
        "/special_dialogue_pending", "/manual_separation/transfer_pending", "/featured_visit/pending",
        "/golden_chest/leap_pending", "/sandman/leap_pending", "/gold_income/pending", "/bull_cave/leap_pending",
        "/steel_trial/pending", "/repel_forces/pending", "/bounty_cycle/transfer_pending", "/fishing/casting_pending",
        "/fordraig/pending", "/cave_of_separation/pending", "/fishing/reward_pending", "/fishing/transfer_pending"})
        facts[J::json_pointer(pointer)] = false;
    return facts;
}
inline int run(const fs::path &root) {
    using namespace wvd;
    need(!fs::exists(root), "EXECUTION_SLOT_TEST_REQUIRES_FRESH_ROOT");
    fs::create_directories(root / "data");
    fs::create_directories(root / "source");
    write(root / "source/definition.json", {{"immutable", true}});
    recognition::Bundle source{root / "source", "source-1", {{"definition.json", platform::file_sha256(root / "source/definition.json")}}};
    games::tasks::PipelineCompiler compiler("execution.cache.test");
    compiler.route("Entry", {"Terminal"});
    auto workflow = compiler.finish();
    const auto data = root / "data";
    const games::tasks::ExecutionPublication slot(data);
    fs::create_directories(data / "published/old-history");
    write(data / "published/old-history/keep.json", {{"old", "untouched"}});
    const auto old_hash = platform::file_sha256(data / "published/old-history/keep.json");
    auto publish = [&](const auto &graph) {
        return games::tasks::publish_native(graph, source, slot.destination(), J::object(), J::object(), nullptr, {}, {}, {},
            [&](const auto &revision) { slot.prepare(revision); });
    };
    std::string revision;
    std::pair<std::size_t, std::uint64_t> package_size;
    std::array<DWORD, 3> original_flow_id{};
    std::vector<std::pair<fs::path, std::string>> historical_results;
    fs::path last_run;
    // Two independent batches, same frozen version. Real publisher, lease,
    // RunStore terminal write and restart-style disk reopen on every iteration.
    for (unsigned batch = 0; batch < 2; ++batch) for (unsigned round = 0; round < 4; ++round) {
        auto publication = publish(workflow);
        const auto request = "batch-" + std::to_string(batch) + "-" + std::to_string(round);
        const auto ordinal = batch * 4 + round + 1;
        if (ordinal == 1) {
            revision = publication.program.revision;
            need(publication.ownership != nullptr, "FIRST_SLOT_HAS_NO_OWNER");
        } else need(!publication.ownership && publication.program.revision == revision &&
            publication.preparation.at("publication_reused") == true &&
            publication.preparation.at("publication_materialized_bytes") == 0 &&
            size(slot.destination().parent_path()) == package_size &&
            file_id(slot.destination() / "program/flow.json") == original_flow_id, "ROUND_CREATED_ANOTHER_PACKAGE");
        storage::RunStore store(data / "runs", "isolated", ordinal,
            {{"engine_kind", "wvd_native"}, {"request_id", request}, {"program_revision", revision}});
        last_run = store.directory();
        auto rollback = slot.bind(last_run, request, revision);
        if (publication.ownership) publication.ownership->accept();
        if (ordinal == 1) {
            // Acceptance removes the temporary preparation intent. Measure
            // steady storage at the same lifecycle boundary as later rounds.
            package_size = size(slot.destination().parent_path());
            original_flow_id = file_id(slot.destination() / "program/flow.json");
        }
        contracts::RunSnapshot snapshot;
        snapshot.run_id = ordinal;
        snapshot.state = contracts::RunState::Completed;
        snapshot.quiescent = true; snapshot.details_complete = true;
        snapshot.business = business();
        store.save_terminal(snapshot, contracts::SessionResult{});
        historical_results.push_back({last_run / "result.json", platform::file_sha256(last_run / "result.json")});
    }
    auto changed = workflow;
    changed.authoring["cache_test_version"] = 2;
    {
        auto held = platform::BundleLease::open_publication(slot.destination());
        rejects([&] { auto ignored = publish(changed); }, "OWNED_DIRECTORY_DELETE_OPEN_FAILED");
        need(held->revision() == revision && fs::exists(slot.destination() / "program/flow.json"), "LEASE_DID_NOT_PROTECT_PACKAGE");
    }
    // Resume a persisted retirement after releasing the last real reader.
    { auto publication = publish(changed); revision = publication.program.revision; publication.ownership->accept(); }
    for (unsigned version = 3; version <= 6; ++version) {
        changed.authoring["cache_test_version"] = version;
        auto publication = publish(changed);
        revision = publication.program.revision;
        publication.ownership->accept();
        need(size(slot.destination().parent_path()).first == package_size.first, "VERSION_COUNT_GROWS_STORAGE");
    }
    // A binding written before input is reversible if worker creation fails.
    {
        storage::RunStore store(data / "runs", "isolated", 20,
            {{"request_id", "never-started"}, {"program_revision", revision}});
        auto undo = slot.bind(store.directory(), "never-started", revision);
        rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_RECORD_UNREADABLE");
        undo();
        slot.prepare(revision);
    }
    {
        storage::RunStore store(data / "runs", "isolated", 21,
            {{"request_id", "protected"}, {"program_revision", revision}});
        auto undo = slot.bind(store.directory(), "protected", revision);
        J terminal{{"run_id", 21}, {"state", "Completed"}, {"quiescent", true}, {"result_saved", true},
            {"details_complete", true}, {"storage_error", ""}, {"business", business()}};
        for (const auto &field : {"quiescent", "result_saved", "details_complete"}) {
            auto bad = terminal; bad[field] = false; write(store.directory() / "result.json", bad);
            rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_UNSETTLED");
        }
        auto unknown = terminal; unknown["unresolved_inputs"] = J::array({{{"delivery", "Unknown"}}});
        write(store.directory() / "result.json", unknown);
        rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_UNSETTLED");
        auto pending = terminal; pending["business"]["inn_payment_pending"] = true;
        write(store.directory() / "result.json", pending);
        rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_UNSETTLED");
        auto mismatch = terminal; mismatch["run_id"] = 22;
        write(store.directory() / "result.json", mismatch);
        rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_RUN_MISMATCH");
        write(store.directory() / "result.json", terminal);
        slot.prepare(revision);
    }
    // Malformed marker and extra files fail closed; no cache miss fallback.
    const auto layout_path = slot.destination() / "publication-layout.json";
    { std::ifstream input(layout_path); auto layout = J::parse(input); input.close();
      write(layout_path, {{"schema", 99}});
      rejects([&] { auto ignored = publish(changed); }, "PUBLICATION_LAYOUT_INVALID");
      write(layout_path, layout); }
    write(slot.destination().parent_path() / "foreign.json", {{"keep", true}});
    rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_UNKNOWN_ENTRY");
    need(fs::remove(slot.destination().parent_path() / "foreign.json"), "OWNED_TEST_FILE_CLEANUP_FAILED");
    const auto state_path = data / "execution-publication/state.json";
    { std::ifstream input(state_path); auto saved = J::parse(input); input.close();
      auto bad = saved; bad["directory_identity"][2] = saved.at("directory_identity").at(2).get<std::uint32_t>() ^ 1u;
      write(state_path, bad);
      rejects([&] { slot.prepare(revision); }, "EXECUTION_PUBLICATION_IDENTITY_CHANGED");
      write(state_path, saved); }
    need(platform::file_sha256(data / "published/old-history/keep.json") == old_hash, "OLD_HISTORY_TOUCHED");
    for (const auto &[path, hash] : historical_results)
        need(platform::file_sha256(path) == hash, "BUSINESS_HISTORY_CHANGED");
    write(root / "result.json", {{"passed", true}, {"same_version_batches", 2}, {"same_version_rounds", 8},
        {"versions", 6}, {"current_revision", revision}, {"package_files", package_size.first}, {"first_package_bytes", package_size.second},
        {"historical_results_unchanged", historical_results.size()}, {"device_inputs", 0},
        {"checks", {"reuse_zero_materialization", "version_retirement", "live_lease_veto", "retirement_resume",
            "start_binding_rollback", "missing_terminal", "unknown_and_pending", "terminal_flags", "identity_mismatch",
            "corrupt_layout", "unknown_entry", "old_history_untouched"}}});
    return 0;
}
inline int reopen(const fs::path &root) {
    std::ifstream input(root / "result.json");
    const auto proof = J::parse(input);
    need(proof.at("passed") == true && proof.at("device_inputs") == 0, "EXECUTION_SLOT_PROOF_REQUIRED");
    const auto state = root / "data/execution-publication/state.json";
    const auto before = wvd::platform::file_sha256(state);
    wvd::games::tasks::ExecutionPublication(root / "data").prepare(proof.at("current_revision").get<std::string>());
    need(wvd::platform::file_sha256(state) == before, "REOPEN_REWROTE_STATE_OR_REPLAYED_RUN");
    return 0;
}
}
