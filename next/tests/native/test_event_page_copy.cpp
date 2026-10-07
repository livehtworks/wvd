#include "app/application.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <thread>

namespace {
thread_local bool count_allocations = false;
thread_local std::uint64_t allocation_count = 0, allocation_bytes = 0;
thread_local std::uint64_t freed_count = 0;
}
void *operator new(std::size_t size) {
    auto *memory = std::malloc(size ? size : 1);
    if (!memory) throw std::bad_alloc();
    if (count_allocations) { ++allocation_count; allocation_bytes += size; }
    return memory;
}
void operator delete(void *memory) noexcept { if (memory && count_allocations) ++freed_count; std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept { ::operator delete(memory); }

namespace wvd::runtime {
struct NativeCoordinatorTestAccess {
    static void seed(NativeRunCoordinator &coordinator, std::shared_ptr<storage::EventJournal> journal) {
        coordinator.journal_ = std::move(journal);
        coordinator.snapshot_.run_id = 1;
        coordinator.snapshot_.state = contracts::RunState::Completed;
    }
};
}
namespace wvd::app {
struct ApplicationAssemblyTestAccess {
    static void seed(Application &app, std::shared_ptr<storage::EventJournal> journal) {
        runtime::NativeCoordinatorTestAccess::seed(*app.coordinator_, std::move(journal));
    }
    static void seed_paths(Application &app, int extra) {
        app.active_pipeline_to_node_ = {{"nested-event", "visible-node"}};
        app.active_source_paths_ = {{"nested-event", nlohmann::json::array({"task", "visible-node"})}};
        for (int i = 0; i < extra; ++i) {
            const auto key = "unused-node-" + std::to_string(i);
            app.active_pipeline_to_node_[key] = key;
            app.active_source_paths_[key] = nlohmann::json::array({"task", "nested-flow", key});
        }
    }
};
}

namespace {
using J = nlohmann::json;
void require(bool value, const char *reason) { if (!value) throw std::runtime_error(reason); }

// Exact former read/response copy pattern, using the same immutable ring values.
J legacy_read(const J &golden, std::mutex &mutex) {
    std::lock_guard lock(mutex);
    J rows = J::array();
    for (const auto &event : golden.at("events")) rows.push_back(event);
    return {{"last_seq", golden.at("last_seq")}, {"resync_required", golden.at("resync_required")}, {"events", rows}};
}
struct Measurement { std::uint64_t count, bytes; double microseconds; };
template<class F> Measurement measure(F operation) {
    allocation_count = allocation_bytes = freed_count = 0;
    const auto began = std::chrono::steady_clock::now();
    count_allocations = true;
    try { for (int i = 0; i < 10; ++i) { auto value = operation(); require(value.is_object(), "INVALID_RESPONSE"); } }
    catch (...) { count_allocations = false; throw; }
    count_allocations = false;
    require(allocation_count == freed_count, "MEASUREMENT_RESPONSE_NOT_FULLY_DESTROYED");
    return {allocation_count, allocation_bytes,
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - began).count()};
}
J metrics(const Measurement &m) { return {{"allocations", m.count}, {"allocated_bytes", m.bytes}, {"microseconds_10_calls", m.microseconds}}; }
void ordered(const J &page) {
    std::uint64_t previous = 0;
    for (const auto &event : page.at("events")) {
        const auto seq = event.at("seq").get<std::uint64_t>();
        require(seq > previous && seq <= page.at("last_seq").get<std::uint64_t>(), "EVENT_ORDER_OR_CURSOR_CHANGED");
        previous = seq;
    }
}
}
int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--replay-memory") {
            const auto output = std::filesystem::absolute(argv[3]);
            require(!std::filesystem::exists(output), "ISOLATED_RESULT_ALREADY_EXISTS");
            require(std::filesystem::file_size(argv[2]) <= 128 * 1024 * 1024, "REPLAY_INPUT_TOO_LARGE");
            std::ofstream report(output);
            for (int cycle = 0; cycle < 6; ++cycle) {
                std::uint64_t events = 0, queries = 0;
                {
                    wvd::storage::EventJournal journal("recorded-replay", cycle + 1, 1024);
                    std::ifstream input(argv[2]);
                    require(bool(input), "REPLAY_INPUT_MISSING");
                    std::string line;
                    while (std::getline(input, line)) {
                        const auto row = J::parse(line);
                        if (row.at("type") == "run.terminal") continue;
                        journal.emit(row.at("session_generation"), row.at("type"), row.at("payload"));
                        if (++events % 32 == 0) {
                            const auto page = journal.read();
                            require(!page.dump().empty(), "REPLAY_RESPONSE_EMPTY");
                            ++queries;
                        }
                    }
                    require(input.eof() && events > 1024, "REPLAY_INPUT_INCOMPLETE");
                    journal.commit_terminal(1, {{"state", "Completed"}}, [](const J &page) {
                        require(!page.dump().empty(), "REPLAY_TERMINAL_EMPTY");
                    });
                }
                const auto heap = wvd::platform::optimize_idle_heap();
                require(heap.succeeded && heap.heap_after.complete && heap.after.process_ok, "REPLAY_MEMORY_UNAVAILABLE");
                const auto after_summary = wvd::platform::sample_memory();
                report << J{{"cycle", cycle + 1}, {"events", events}, {"queries", queries},
                    {"private_bytes", heap.after.private_bytes}, {"allocated_bytes", heap.heap_after.allocated},
                    {"private_after_summary", after_summary.private_bytes},
                    {"committed_bytes", heap.heap_after.committed}, {"summary_us", heap.heap_after.elapsed_us},
                    {"scope", "real recorded event payloads through production journal, no device or OCR"}}.dump() << '\n';
                report.flush();
                require(bool(report), "REPLAY_REPORT_WRITE_FAILED");
            }
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--image-index") {
            const auto output = std::filesystem::absolute(argv[2]);
            require(!std::filesystem::exists(output), "ISOLATED_RESULT_ALREADY_EXISTS");
            auto make_workflow = [](int depth) {
                wvd::games::tasks::CompiledWorkflow workflow;
                workflow.kind = "image-index-fixture";
                J probes = J::array();
                for (int i = 0; i < 256; ++i)
                    probes.push_back({{"mode", "template"}, {"image", "dungFlag"},
                        {"roi", {0, 0, 900, 1600}}, {"threshold", 0.8}});
                J condition{{"mode", "any"}, {"conditions", std::move(probes)}};
                for (int i = 0; i < depth; ++i)
                    condition = {{"mode", "all"}, {"conditions", J::array({std::move(condition)})}};
                workflow.nodes = {
                    {"Entry", {{"observation", "Registered"}, {"recognizer", "WvdVision"},
                        {"observation_args", std::move(condition)}, {"operation", "Route"}, {"next", {"Terminal"}}}},
                    {"Terminal", {{"operation", "Registered"}, {"binding", "Finish"}}}};
                workflow.refresh_images();
                return workflow;
            };
            auto flat = make_workflow(0), deep = make_workflow(12);
            const auto flat_cost = measure([&] { flat.refresh_images(); return J{{"images", flat.images}}; });
            const auto deep_cost = measure([&] { deep.refresh_images(); return J{{"images", deep.images}}; });
            require(flat.images == deep.images && flat.images == std::vector<std::string>{"dungFlag.png"},
                    "IMAGE_INDEX_CONTENT_CHANGED");
            // Nesting must not cause another copy of the full leaf collection at each parent.
            require(deep_cost.bytes <= flat_cost.bytes + 4096 && deep_cost.count <= flat_cost.count + 128,
                    "IMAGE_INDEX_RECOPIES_PARENT_SUBTREES");
            std::ofstream report(output);
            report << J{{"passed", true}, {"flat", metrics(flat_cost)}, {"nested_12", metrics(deep_cost)},
                {"images", deep.images}, {"scope", "C++ allocations in actual refresh_images; not retained heap attribution"}}.dump(2);
            report.close(); require(bool(report), "IMAGE_INDEX_RESULT_WRITE_FAILED");
            std::cout << "Image dependency scan preserves index without depth-multiplied subtree copies\n";
            return 0;
        }
        if (argc != 4) throw std::runtime_error("USAGE: evidence-root pack-root quest-catalog");
        const auto root = std::filesystem::absolute(argv[1]);
        require(!std::filesystem::exists(root), "ISOLATED_ROOT_ALREADY_EXISTS");
        std::filesystem::create_directories(root);
        auto journal = std::make_shared<wvd::storage::EventJournal>("copy-fixture", 1, 1024);
        const auto empty = journal->read();
        require(empty.at("last_seq") == 0 && empty.at("events").empty() && !empty.at("resync_required"), "EMPTY_RING_CHANGED");
        const J payload = {{"node_id", "nested-event"}, {"details", {{"rows", J::array({
            {{"name", std::string(128, 'x')}, {"values", J::array({1, 2, 3, 4})}},
            {{"name", std::string(128, 'y')}, {"values", J::array({5, 6, 7, 8})}}})}}}};
        for (int i = 0; i < 1024; ++i) journal->emit(1, "node.completed", payload, i == 0);
        const auto golden = journal->read();
        std::mutex baseline_mutex;
        require(golden == legacy_read(golden, baseline_mutex), "READ_RESPONSE_CHANGED");
        const auto old_read = measure([&] { return legacy_read(golden, baseline_mutex); });
        const auto new_read = measure([&] { return journal->read(); });
        auto legacy_response = [&] { J value = J::object(); const auto page = legacy_read(golden, baseline_mutex); value["events"] = page; return value; };
        auto current_response = [&] { J value = J::object(); value["events"] = journal->read(); return value; };
        require(legacy_response() == current_response(), "ASSEMBLED_EVENT_RESPONSE_CHANGED");
        const auto old_response = measure(legacy_response);
        const auto new_response = measure(current_response);
        require(new_read.count < old_read.count && new_read.bytes < old_read.bytes, "JOURNAL_COPY_NOT_REDUCED");
        require(new_response.count < old_response.count && new_response.bytes < old_response.bytes, "RESPONSE_COPY_NOT_REDUCED");
        auto cursor = journal->read(512);
        require(cursor.at("events").size() == 512 && cursor.at("events")[0].at("seq") == 513, "CURSOR_CHANGED");
        require(journal->read(1024).at("events").empty(), "EMPTY_CURSOR_CHANGED");

        wvd::app::Application app({root / "data", std::filesystem::absolute(argv[2]), {}, std::filesystem::absolute(argv[3])});
        wvd::app::ApplicationAssemblyTestAccess::seed(app, journal);
        wvd::api::Request request{wvd::api::http::verb::get, "/api/v1/runs/current", 11};
        const auto reply = app.handle(request);
        const auto response = J::parse(reply.body);
        require(static_cast<unsigned>(reply.status) == 200 && response.at("events") == golden, "REAL_STATUS_API_EVENTS_CHANGED");
        require(response.at("run_id") == 1 && response.at("state") == "Completed", "REAL_STATUS_API_STATE_CHANGED");
        require(response.at("step_name") == "nested-event" && response.at("execution_stage") == "pipeline", "REVERSE_SCAN_CHANGED");

        wvd::app::ApplicationAssemblyTestAccess::seed_paths(app, 0);
        auto status = [&] {
            const auto reply = app.handle(request);
            require(static_cast<unsigned>(reply.status) == 200, "STATUS_REQUEST_FAILED");
            return J::parse(reply.body);
        };
        const auto indexed = status();
        require(indexed.at("current_node_id") == "visible-node" && indexed.at("events") == golden,
                "STATUS_NODE_MAPPING_CHANGED");
        const auto one_path_cost = measure(status);
        wvd::app::ApplicationAssemblyTestAccess::seed_paths(app, 2561);
        require(status() == indexed, "UNUSED_PATHS_CHANGED_STATUS");
        const auto many_paths_cost = measure(status);
        require(many_paths_cost.bytes <= one_path_cost.bytes + 4096 &&
                many_paths_cost.count <= one_path_cost.count + 64,
                "STATUS_RECOPIES_ENTIRE_PATH_INDEX");
        std::ofstream index_report(root / "status-index-allocations.json");
        index_report << J{{"passed", true}, {"one_path", metrics(one_path_cost)},
            {"extra_2561_paths", metrics(many_paths_cost)},
            {"scope", "actual status API allocation traffic, not retained memory"}}.dump(2);
        index_report.close(); require(bool(index_report), "STATUS_INDEX_REPORT_WRITE_FAILED");

        std::jthread emitter([&] { for (int i = 0; i < 32; ++i) journal->emit(1, "node.completed", payload); });
        for (int i = 0; i < 10; ++i) ordered(journal->read());
        emitter.join();
        auto overflow = journal->read();
        require(overflow.at("resync_required") && overflow.at("events")[0].at("seq") == 1, "CRITICAL_OR_RESYNC_CHANGED");
        bool persisted = false;
        journal->commit_terminal(1, {{"state", "Completed"}}, [&](const auto &) { persisted = true; });
        const auto terminal = journal->read();
        require(persisted && terminal.at("events").size() == 1025 && terminal.at("events").back().at("payload").at("state") == "Completed", "TERMINAL_RETENTION_CHANGED");
        ordered(terminal);
        wvd::storage::EventJournal failed_terminal("failed-terminal", 2, 1024);
        failed_terminal.emit(1, "node.completed", payload);
        const auto before_failure = failed_terminal.read();
        bool rejected = false;
        try { failed_terminal.commit_terminal(1, {{"state", "Completed"}}, [](const auto &) { throw std::runtime_error("fixture-write-failure"); }); }
        catch (const std::runtime_error &) { rejected = true; }
        require(rejected && failed_terminal.read() == before_failure, "FAILED_TERMINAL_CHANGED_HISTORY");
        const J result = {{"complete", true}, {"fixture_events", 1024}, {"calls_per_measurement", 10},
            {"event_page_serialized_bytes", golden.dump().size()}, {"responses_destroyed_each_call", true},
            {"legacy_journal", metrics(old_read)}, {"current_journal", metrics(new_read)},
            {"legacy_event_response", metrics(old_response)}, {"current_event_response", metrics(new_response)},
            {"real_status_api", true}, {"cursor_resync_critical_terminal_concurrency", true},
            {"journal_read_time_includes_uncontended_lock_and_construction", true},
            {"leak_attribution", "UNRESOLVED"}};
        std::ofstream(root / "result.json") << result.dump(2);
        std::cout << result.dump(2) << '\n';
    } catch (const std::exception &error) { count_allocations = false; std::cerr << error.what() << '\n'; return 1; }
}
