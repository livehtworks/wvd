#pragma once
#include "app/publication_maintenance.hpp"
#include "app/service_instance.hpp"
#include "games/wvd/tasks/native_publisher.hpp"
#include "platform/windows/fixed_directory_move.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/runtime_files.hpp"
#include "storage/submission_store.hpp"
#include "storage/run_store.hpp"
#include <fstream>
#include <thread>
#include <winioctl.h>
#include <cstring>
#include <cstddef>
#include <limits>

namespace wvd::games::tasks {
struct NativePublicationTestAccess {
    static void available(std::optional<std::uint64_t> value) { NativePublication::available_override_ = value; }
};
}

namespace publication_maintenance_contract {
namespace fs = std::filesystem;
using J = nlohmann::json;
using namespace wvd;
using M = app::PublicationMaintenance;
inline void check(bool value, const char *code) { if (!value) throw std::runtime_error(code); }
inline void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path()); platform::atomic_write(path, text, false);
}
inline J read(const fs::path &path) { std::ifstream input(path, std::ios::binary); return J::parse(input); }
template<class F> inline void blocked(F action, const char *code) {
    bool rejected{}; try { action(); } catch (const std::exception &) { rejected = true; }
    check(rejected,code);
}
inline void junction(const fs::path &link, const fs::path &target) {
    fs::create_directory(link);
    const auto substitute = std::wstring(L"\\??\\") + target.native();
    const auto display = target.native();
    struct Mount { ULONG tag; USHORT length,reserved,so,sl,po,pl; WCHAR names[1]; };
    const auto size = offsetof(Mount,names) + (substitute.size() + display.size() + 2) * sizeof(WCHAR);
    std::vector<std::max_align_t> memory((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *m = reinterpret_cast<Mount *>(memory.data()); m->tag = IO_REPARSE_TAG_MOUNT_POINT;
    m->length = static_cast<USHORT>(size - 8); m->reserved = 0; m->so = 0;
    m->sl = static_cast<USHORT>(substitute.size() * 2); m->po = static_cast<USHORT>((substitute.size() + 1) * 2);
    m->pl = static_cast<USHORT>(display.size() * 2);
    std::memcpy(m->names,substitute.c_str(),(substitute.size()+1)*2);
    std::memcpy(reinterpret_cast<char *>(m->names)+m->po,display.c_str(),(display.size()+1)*2);
    const auto h = CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(h != INVALID_HANDLE_VALUE,"C09_JUNCTION_OPEN"); DWORD count{};
    const bool ok = DeviceIoControl(h,FSCTL_SET_REPARSE_POINT,m,static_cast<DWORD>(size),nullptr,0,&count,nullptr) != FALSE;
    CloseHandle(h); check(ok,"C09_JUNCTION_CREATE");
}
template<class F> inline void rejects(F action, const std::string &code) {
    try { action(); } catch (const std::exception &error) {
        if (std::string(error.what()).starts_with(code)) return;
        throw std::runtime_error("C09_WRONG_REJECTION:expected=" + code + ";actual=" + error.what());
    }
    throw std::runtime_error("C09_EXPECTED_REJECTION:" + code);
}
inline int child(const fs::path &root, const std::string &action, const std::string &id, const std::string &pause) {
    M::execute(root, action, id, [&](const J &event) {
        if (event.at("phase") == pause) {
            write(root / "checkpoint.json", event.dump());
            Sleep(INFINITE); // The parent terminates this real maintenance process.
        }
    });
    return 0;
}
inline J kill_at(const fs::path &root, const std::string &action, const std::string &id, const std::string &phase) {
    wchar_t executable[32768]{}; check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "C09_EXE_PATH");
    auto command = std::wstring(L"\"") + executable + L"\" --publication-maintenance-child \"" + root.native() +
        L"\" " + platform::path_from_utf8(action).native() + L" " + platform::path_from_utf8(id).native() +
        L" " + platform::path_from_utf8(phase).native();
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process), "C09_CHILD_CREATE");
    CloseHandle(process.hThread);
    bool ready{};
    for (unsigned i = 0; i < 400; ++i) {
        if (fs::exists(root / "checkpoint.json")) { ready = true; break; }
        if (WaitForSingleObject(process.hProcess, 25) == WAIT_OBJECT_0) break;
    }
    if (!ready) {
        DWORD exit{}; GetExitCodeProcess(process.hProcess, &exit);
        TerminateProcess(process.hProcess, 91); WaitForSingleObject(process.hProcess, 5000); CloseHandle(process.hProcess);
        throw std::runtime_error("C09_CHECKPOINT_NOT_REACHED:" + phase + ":" + std::to_string(exit));
    }
    check(TerminateProcess(process.hProcess, 90), "C09_CHILD_TERMINATE");
    check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "C09_CHILD_JOIN");
    CloseHandle(process.hProcess);
    return read(root / "checkpoint.json");
}
inline J fixture(const fs::path &root, const platform::BundleLease &source) {
    fs::create_directories(root / "published" / "staging-demo");
    for (const auto &[name, hash] : source.manifest()) {
        (void)hash; const auto target = root / "published" / "staging-demo" / platform::BundleLease::checked_relative(name);
        fs::create_directories(target.parent_path()); source.copy_member(name, target);
    }
    fs::create_directories(root / "requests"); fs::create_directories(root / "runs");
    const J definition{{"request_id","staging-demo"},{"program_revision",source.revision()},
        {"pack_revision",source.revision()},{"unique_program_count",1},{"handoff_parent",nullptr}};
    {
        storage::RunStore store(root / "runs", "fixture", 1, definition);
        contracts::RunSnapshot snapshot; snapshot.run_id = 1; snapshot.generation = 1;
        snapshot.state = contracts::RunState::Completed; snapshot.quiescent = true;
        snapshot.result_saved = true; snapshot.details_complete = true;
        contracts::SessionResult session;
        store.save_terminal(snapshot, session);
    }
    {
        storage::SubmissionStore requests(root / "requests");
        requests.save("staging-demo", {{"kind","task"},{"request",{{"request_id","staging-demo"},{"repeat",false}}}},
            {{"request_id","staging-demo"},{"state","submitted"},{"run_id",1}}, false);
    }
    auto plan = M::execute(root,"plan","staging-demo");
    J raw = J::object();
    for (const auto &sub : {"requests","runs"})
        for (const auto &entry : fs::recursive_directory_iterator(root / sub))
            if (entry.is_regular_file()) raw[platform::utf8(entry.path().lexically_relative(root))] = platform::file_sha256(entry.path());
    plan["fixture_raw_history"] = raw;
    return plan;
}
inline void unchanged_history(const fs::path &root, const J &plan) {
    J actual = J::object();
    for (const auto &sub : {"requests","runs"})
        for (const auto &entry : fs::recursive_directory_iterator(root / sub))
            if (entry.is_regular_file()) actual[platform::utf8(entry.path().lexically_relative(root))] = platform::file_sha256(entry.path());
    check(actual == plan.at("fixture_raw_history"),"C09_RAW_REQUEST_RUN_BYTES_CHANGED");
}
inline int cli_fixture(const fs::path &root) {
    check(root.is_absolute() && !fs::exists(root), "C09_CLI_FRESH_ROOT_REQUIRED");
    fs::create_directories(root);
    recognition::Bundle baseline{root / "input", "c09-tiny-original", {}};
    write(baseline.root / "models/tiny.onnx", "not-an-ORT-model");
    write(baseline.root / "definition.json", "{\"schema\":1}");
    for (const auto &name : {"models/tiny.onnx","definition.json"})
        baseline.files.push_back({name,platform::file_sha256(baseline.root / name)});
    games::tasks::PipelineCompiler compiler("maintenance.fixture"); compiler.route("Entry", {"Terminal"});
    recognition::Bundle mod{root / "mod","tiny-mod-provenance",{}};
    write(mod.root / "image/unused.png","opaque tiny mod fixture; never decoded");
    mod.files.push_back({"image/unused.png",platform::file_sha256(mod.root / "image/unused.png")});
    auto source = games::tasks::publish_native(compiler.finish(),baseline,root / "seed/published/one",J::object(),J::object(),&mod);
    source.ownership->accept(); source.ownership->receipt()->require_clean_acceptance();
    const auto data = root / "data";
    const auto plan = fixture(data,*source.bundle.lease);
    check(!fs::exists(data / ".publication-maintenance.json") && !fs::exists(data / ".publication-maintenance-journal.json") &&
        !fs::exists(platform::path_from_utf8(plan.at("trash_namespace"))),"C09_CLI_FIXTURE_CREATED_TRANSACTION");
    write(root / "web/index.html","<!doctype html><title>Isolated maintenance guard smoke</title>");
    std::uint64_t bytes{}; unsigned files{};
    for (const auto &entry : fs::recursive_directory_iterator(root)) if (entry.is_regular_file()) { bytes += entry.file_size(); ++files; }
    check(bytes < 1024*1024 && files < 80,"C09_CLI_FIXTURE_BUDGET");
    J result{{"data_root",platform::utf8(data)},{"request_id","staging-demo"},{"eligible_plan",plan},
        {"web_root",platform::utf8(root / "web")},{"missing_pack_root",platform::utf8(root / "missing-pack")},
        {"missing_quests",platform::utf8(root / "missing-quests.json")},{"logical_bytes_before_result",bytes},
        {"files_before_result",files},{"conversion_executed",false},{"application_or_device_constructed",false}};
    write(root / "result.json",result.dump(2));
    std::cout << result.dump() << '\n';
    return 0;
}
inline int run(const fs::path &root) {
    check(root.is_absolute() && !fs::exists(root), "C09_FRESH_ROOT_REQUIRED");
    fs::create_directories(root);
    recognition::Bundle baseline{root / "input", "c09-tiny-original", {}};
    write(baseline.root / "models/tiny.onnx", "not-an-ORT-model");
    write(baseline.root / "definition.json", "{\"schema\":1}");
    for (const auto &name : {"models/tiny.onnx","definition.json"})
        baseline.files.push_back({name,platform::file_sha256(baseline.root / name)});
    games::tasks::PipelineCompiler compiler("maintenance.fixture"); compiler.route("Entry", {"Terminal"});
    const auto compiled = compiler.finish();
    recognition::Bundle mod{root / "mod","tiny-mod-provenance",{}};
    write(mod.root / "image/unused.png","opaque tiny mod fixture; never decoded");
    mod.files.push_back({"image/unused.png",platform::file_sha256(mod.root / "image/unused.png")});
    auto first = games::tasks::publish_native(compiled, baseline, root / "seed/published/one", J::object(),J::object(),&mod);
    first.ownership->accept(); first.ownership->receipt()->require_clean_acceptance();
    auto second = games::tasks::publish_native(compiled, baseline, root / "seed/published/two", J::object(),J::object(),&mod);
    second.ownership->accept(); second.ownership->receipt()->require_clean_acceptance();
    check(first.bundle.lease->resource_digest() == second.bundle.lease->resource_digest() &&
        second.preparation.at("resource_reused") == true, "C09_NORMAL_WRITER_REUSE");
    J results = J::array();
    std::vector<fs::path> trash_roots;
    const auto make_fixture = [&](const fs::path &data) {
        auto plan = fixture(data,*first.bundle.lease);
        trash_roots.push_back(platform::path_from_utf8(plan.at("trash_namespace")));
        return plan;
    };
    const std::vector<std::string> phases{"intent","trash_registered","create_stage_intent","stage_registered","preparing","prepared","move_old_intent",
        "old_moved","old_revalidated","move_new_intent","new_moved","committed"};
    for (const auto &phase : phases) {
        const auto data = root / phase;
        const auto plan = make_fixture(data);
        const auto event = kill_at(data, "convert", "staging-demo", phase);
        if (phase != "committed") rejects([&] { M::require_no_pending(data); }, "PUBLICATION_MAINTENANCE_PENDING");
        if (phase == "preparing") {
            games::tasks::NativePublicationTestAccess::available(0);
            rejects([&] { M::execute(data,"finish",event.at("transaction_id")); },"NATIVE_PUBLICATION_SPACE_LOW");
            games::tasks::NativePublicationTestAccess::available({});
            check(M::execute(data,"status",event.at("transaction_id")).at("phase") == "preparing" &&
                platform::fixed_directory_identity(data / "published/staging-demo") == plan.at("old").at("directory_id").get<std::string>(),
                "C09_LOW_SPACE_CHANGED_AUTHORITY");
        }
        const auto complete = M::execute(data, "finish", event.at("transaction_id"));
        check(complete.at("phase") == "committed", "C09_FINISH_FAILED");
        M::require_no_pending(data);
        const auto converted = platform::BundleLease::open_publication(data / "published/staging-demo");
        check(converted->revision() == first.bundle.lease->revision() && converted->manifest() == first.bundle.lease->manifest() &&
            !converted->resource_digest().empty(), "C09_RAW_CONVERSION_MISMATCH");
        unchanged_history(data, plan);
        // Release before the explicit fixed-ID rollback.
        const auto old_id = plan.at("old").at("directory_id");
        results.push_back({{"checkpoint",phase},{"real_process_killed",true},{"finish","committed"},{"old_id",old_id}});
    }
    for (const auto &phase : {"rollback_intent","rollback_new_moved","rollback_old_intent"}) {
        const auto data = root / phase; const auto plan = make_fixture(data);
        const auto complete = M::execute(data, "convert", "staging-demo");
        const auto event = kill_at(data, "rollback", complete.at("transaction_id"), phase);
        const auto rolled = M::execute(data, "rollback", event.at("transaction_id"));
        check(rolled.at("phase") == "rolled_back" && platform::fixed_directory_identity(data / "published/staging-demo") ==
            plan.at("old").at("directory_id").get<std::string>(), "C09_ROLLBACK_ID_CHANGED");
        check(platform::BundleLease::open_publication(data / "published/staging-demo")->resource_digest().empty(), "C09_ROLLBACK_NOT_LEGACY");
        M::require_no_pending(data); unchanged_history(data, plan);
        results.push_back({{"checkpoint",phase},{"real_process_killed",true},{"rollback","rolled_back"}});
    }
    for (const auto &phase : {"sentinel_persisted","stage_created_unregistered"}) {
        const auto data = root / phase; const auto plan = make_fixture(data);
        const auto event = kill_at(data, "convert", "staging-demo", phase);
        check(platform::fixed_directory_identity(data / "published/staging-demo") == plan.at("old").at("directory_id").get<std::string>(), "C09_UNKNOWN_MOVED_OLD");
        if (std::string(phase) == "stage_created_unregistered") {
            rejects([&] { M::execute(data,"finish",event.at("transaction_id")); }, "PUBLICATION_MAINTENANCE_STAGE_ID_UNKNOWN");
            rejects([&] { M::execute(data,"rollback",event.at("transaction_id")); }, "PUBLICATION_MAINTENANCE_STAGE_ID_UNKNOWN");
            const auto stage = data / "published" / ("staging-demo.conversion-stage-" + event.at("transaction_id").get<std::string>());
            check(fs::exists(stage) && fs::is_empty(stage), "C09_UNKNOWN_STAGE_LOST");
        } else rejects([&] { M::execute(data,"status",event.at("transaction_id")); }, "PUBLICATION_MAINTENANCE_READ_FAILED");
        unchanged_history(data,plan);
        results.push_back({{"checkpoint",phase},{"real_process_killed",true},{"recovery","explicit_unknown_fail_closed"}});
    }
    const auto locked = root / "locked"; const auto locked_plan = make_fixture(locked);
    { app::ServiceInstance service(locked);
      rejects([&] { M::execute(locked,"convert","staging-demo"); },"SERVICE_DATA_ROOT_LOCKED"); }
    check(!fs::exists(locked / ".publication-maintenance.json"), "C09_LOCK_REJECTION_WROTE");
    unchanged_history(locked,locked_plan);
    // Existing valid history data is altered only inside this owned fixture.
    const auto result_path = locked / "runs/fixture/1/result.json";
    auto stopped = read(result_path); stopped["state"] = "UserStopped";
    platform::atomic_write(result_path,stopped.dump(),true);
    rejects([&] { M::execute(locked,"convert","staging-demo"); },"PUBLICATION_RUN_NOT_SETTLED");
    check(!fs::exists(locked / ".publication-maintenance.json"), "C09_STOPPED_MUTATED_PUBLICATION");
    results.push_back({{"live_service_lock","rejected_without_sentinel"},{"UserStopped","preserved"}});
    // Check corruption/missing-pair gates against an already committed case,
    // preserving both original bytes after each deliberate isolated mutation.
    const auto paired = root / "intent";
    const auto sentinel_path = paired / ".publication-maintenance.json";
    const auto sentinel = read(sentinel_path);
    const auto journal_file = paired / ".publication-maintenance-journal.json";
    const auto journal = read(journal_file);
    platform::atomic_write(sentinel_path,"{}",true);
    blocked([&] { M::require_no_pending(paired); },"C09_CORRUPT_SENTINEL_ACCEPTED");
    platform::atomic_write(sentinel_path,sentinel.dump(),true);
    auto mismatch = journal; mismatch["journal_id"] = "wrong-journal";
    platform::atomic_write(journal_file,mismatch.dump(),true);
    blocked([&] { M::require_no_pending(paired); },"C09_MISMATCHED_JOURNAL_ACCEPTED");
    platform::atomic_write(journal_file,journal.dump(),true);
    fs::rename(journal_file,paired / "journal-preserved.json");
    blocked([&] { M::require_no_pending(paired); },"C09_MISSING_JOURNAL_ACCEPTED");
    fs::rename(paired / "journal-preserved.json",journal_file);
    fs::rename(sentinel_path,paired / "sentinel-preserved.json");
    blocked([&] { M::require_no_pending(paired); },"C09_MISSING_SENTINEL_ACCEPTED");
    fs::rename(paired / "sentinel-preserved.json",sentinel_path);
    M::require_no_pending(paired);
    auto missing_registration = journal; missing_registration["transactions"] = J::object();
    platform::atomic_write(journal_file,missing_registration.dump(),true);
    blocked([&] { M::require_no_pending(paired); },"C09_LOST_REGISTRATION_ACCEPTED");
    platform::atomic_write(journal_file,journal.dump(),true);
    { auto legacy = platform::BundleLease::open_publication(locked / "published/staging-demo");
      rejects([&] { games::tasks::admit_existing_publication(*legacy,locked / "published/new-stage",(std::numeric_limits<std::uint64_t>::max)()); },
          "NATIVE_PUBLICATION_BUDGET_OVERFLOW"); }
    results.push_back({{"guard","corrupt_missing_mismatched_pairs_rejected"}});
    const auto old_tx = journal.at("transactions").begin().key();
    M::execute(paired,"rollback",old_tx);
    const auto second_transaction = M::execute(paired,"convert","staging-demo");
    const auto registered = read(journal_file);
    check(registered.at("transactions").size() == 2 && registered.at("transactions").contains(old_tx) &&
        registered.at("transactions").contains(second_transaction.at("transaction_id").get<std::string>()),"C09_PRIOR_RECOVERY_MAPPING_LOST");
    auto lost_old = registered; lost_old["transactions"].erase(old_tx);
    platform::atomic_write(journal_file,lost_old.dump(),true);
    blocked([&] { M::require_no_pending(paired); },"C09_OLD_MAPPING_LOSS_ACCEPTED");
    platform::atomic_write(journal_file,registered.dump(),true);
    std::size_t max_entry{};
    for (const auto &[tx,entry] : registered.at("transactions").items()) {
        (void)tx; max_entry = (std::max)(max_entry,entry.dump().size());
    }
    results.push_back({{"global_registered_transactions",2},{"observed_max_entry_bytes",max_entry},
        {"estimated_1334_entries_bytes",1334ULL*(max_entry+128)+4096},{"hard_transaction_cap",2048},{"hard_journal_bytes_cap",16*1024*1024}});
    const auto tampered = root / "trash-content-tampered"; const auto tamper_plan = make_fixture(tampered);
    const auto interrupted = kill_at(tampered,"convert","staging-demo","old_moved");
    const auto state = M::execute(tampered,"status",interrupted.at("transaction_id"));
    const auto original_in_trash = platform::path_from_utf8(state.at("trash").at("original"));
    platform::atomic_write(original_in_trash / "models/tiny.onnx","tampered-after-move",true);
    blocked([&] { M::execute(tampered,"finish",interrupted.at("transaction_id")); },"C09_TAMPERED_TRASH_ACTIVATED");
    check(!fs::exists(tampered / "published/staging-demo"),"C09_NEW_TREE_ACTIVATED_BEFORE_TRASH_VERIFICATION");
    unchanged_history(tampered,tamper_plan);
    results.push_back({{"moved_old_content_tampered","finish_rejected_before_activation"}});
    // Same-handle move protects old directory identity and rejects occupied and
    // reparse destinations; its destructor never deletes either directory.
    const auto moves = root / "moves"; fs::create_directories(moves / "source"); fs::create_directory(moves / "occupied");
    const auto source_id = platform::fixed_directory_identity(moves / "source");
    rejects([&] { platform::move_fixed_directory(moves / "source",moves / "occupied",source_id); },"PUBLICATION_MOVE_REJECTED");
    rejects([&] { platform::move_fixed_directory(moves / "source",moves / "unused","wrong-id"); },"PUBLICATION_MOVE_ID_MISMATCH");
    junction(moves / "link",moves / "occupied");
    rejects([&] { platform::move_fixed_directory(moves / "source",moves / "link/child",source_id); },"PUBLICATION_MOVE_DIRECTORY_INVALID");
    check(platform::fixed_directory_identity(moves / "source") == source_id && fs::is_directory(moves / "occupied"),"C09_MOVE_REJECTION_DELETED");
    results.push_back({{"move","occupied_identity_and_real_reparse_rejected"}});
    std::uint64_t bytes{}; unsigned files{};
    const auto count_tree = [&](const fs::path &tree) {
        if (!fs::exists(tree)) return;
        for (const auto &entry : fs::recursive_directory_iterator(tree)) {
            if (GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) {
                check(entry.path() == moves / "link","C09_UNEXPECTED_REPARSE"); continue;
            }
            if (entry.is_regular_file()) { bytes += entry.file_size(); ++files; }
        }
    };
    count_tree(root);
    J trash_list = J::array();
    for (const auto &tree : trash_roots) if (fs::exists(tree)) { count_tree(tree); trash_list.push_back(platform::utf8(tree)); }
    check(bytes < 20 * 1024 * 1024 && files < 500, "C09_FIXTURE_BUDGET_EXCEEDED");
    write(root / "result.json",J{{"passed",true},{"cases",results},{"logical_bytes_before_result",bytes},
        {"files_before_result",files},{"known_trash_roots",trash_list},{"real_models_or_frames_copied",false},{"formal_data_used",false},
        {"history_fixture","RunStore::save_terminal; no game execution"}}.dump(2));
    std::cout << "PASS C09 tiny native publication conversion, explicit recovery and true process interruption\n";
    return 0;
}
}
