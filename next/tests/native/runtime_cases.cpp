#include "runtime_fixture.hpp"

namespace fixture {
J runtime_case(const std::string &name, Setup &s) {
    using namespace contracts;
    runtime::RunCoordinator coordinator(s.output);
    Unblock safety{s.device};
    auto d = s.definition();
    if (name == "session-restart-refused") {
        storage::EventJournal events(platform::unique_id(), 1);
        runtime::ExecutionSession session(d.initial, *s.device, d.policy, 1, 1, events);
        session.start();
        require(session.wait_for(10000ms), "finite session did not finish");
        auto result = session.join();
        require(result.end == SessionEnd::Completed && result.quiescent, "finite session failed");
        try {
            session.start();
            throw std::runtime_error("ended session restarted");
        } catch (const std::runtime_error &e) {
            require(std::string(e.what()) == "SESSION_ALREADY_STARTED", e.what());
        }
        require(s.device->connections == 1 && s.device->calls == 1, "ended session ran again");
        return {{"quiescent", true}, {"connections", 1}, {"backend_called", 1}};
    }
    if (name == "wait-stop")
        d.initial.entry = "Wait";
    if (name == "custom-stop")
        d.initial.entry = "Cooperate";
    if (name == "nested-stop")
        d.initial.entry = "NestedWait";
    if (name == "clone-stop")
        d.initial.entry = "CloneWait";
    if (name == "callback-exception")
        d.initial.entry = "Throw";
    if (name == "child-failure")
        d.initial.entry = "ChildFailure";
    if (name == "root-not-terminal")
        d.initial.entry = "RootNotTerminal";
    if (name == "wrong-root-node")
        d.initial.terminal_node = "NotTerminal";
    if (name == "framework-failure")
        d.initial.entry = "False";
    if (name == "builtin-unguarded")
        d.initial.entry = "BareClick";
    if (name == "postcondition-timeout")
        s.device->change_frame = false;
    if (name == "result-save-failure")
        s.device->block_connect = true;
    if (name == "permission-denied")
        d.policy.permissions.clear();
    if (name == "application-mismatch")
        s.device->application = "another.app";
    if (name == "initialization-failure")
        s.device->connect_ok = false;
    if (name == "stop-timeout" || name == "stop-during-connect") {
        if (name == "stop-timeout")
            s.device->block_input = true;
        else
            s.device->block_connect = true;
    }
    if (name == "held-touch")
        d.initial.entry = "HeldTouch";
    if (name == "held-key")
        d.initial.entry = "HeldKey";
    if (name == "native-scroll")
        d.initial.entry = "NativeScroll";
    if (name == "sequential-reset")
        d.initial.entry = "False";
    if (name == "release-timeout") {
        d.initial.entry = "HeldTouch";
        s.device->reject_release = true;
    }
    if (name == "native-swipe")
        d.initial.entry = "NativeSwipe";
    if (name == "session-time-limit") {
        d.initial.entry = "Cooperate";
        d.initial.time_limit = 150ms;
    }
    if (name == "recovery") {
        d.initial.entry = "Recover";
        d.recovery_limit = 1;
        d.recover = [&](const auto &) { return std::optional{s.definition().initial}; };
    }
    if (name == "unresolved-recovery")
        d.initial.entry = "Recover";
    if (name == "clone-isolation") {
        d.initial.entry = "CloneInspect";
        d.initial.actions["TestClone"] = [](maafw::Context &context, const J &) {
            auto before = context.node_data("Data");
            auto result =
                context.run_child("ChildOK", {{"Data", {{"roi", {11, 22, 33, 44}}}}}, true);
            require(result.valid && result.status == MaaStatus_Succeeded, "clone child failed");
            require(context.node_data("Data") == before, "clone modified parent");
            return true;
        };
    }
    if (name == "interrupt")
        d.initial.entry = "InterruptRoot";
    if (name == "real-device-rejected") {
        s.device->real = true;
        try {
            coordinator.start(d, s.device);
            throw std::runtime_error("real backend accepted");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()) == "REAL_DEVICE_NOT_ENABLED", error.what());
        }
        require(s.device->connections == 0 && s.device->calls == 0, "real-device side effects");
        return {{"connected", 0}, {"backend_called", 0}};
    }
    auto started = coordinator.start(d, s.device);
    if (name == "idempotency-conflict") {
        auto changed = d;
        changed.initial.entry = "False";
        try {
            coordinator.start(changed, s.device);
            throw std::runtime_error("changed duplicate accepted");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()) == "IDEMPOTENCY_CONFLICT", error.what());
        }
    }
    if (name == "duplicate-start") {
        auto duplicate = coordinator.start(d, s.device);
        require(duplicate.run_id == started.run_id, "duplicate created new run");
    }
    if (name == "result-save-failure") {
        // 人为让专属运行结果目标成为目录，真实原子提交必须失败，不能将业务标 Completed。
        until([&] { return s.device->connections.load() > 0; });
        std::filesystem::create_directory(coordinator.run_directory() / "result.json");
        s.device->unblock = true;
    }
    if (name == "wait-stop" || name == "custom-stop" || name == "nested-stop" ||
        name == "clone-stop") {
        until([&] {
            if (name == "wait-stop")
                return s.device->captures.load() >= 2;
            auto evidence = coordinator.events();
            for (const auto &event : evidence["events"])
                if (event["type"] == "custom.enter") {
                    if (name == "custom-stop" && event["payload"]["node"] == "Cooperate")
                        return true;
                    if (name != "custom-stop" && event["payload"]["node"] == "ChildWait" &&
                        event["payload"]["depth"] == 1)
                        return true;
                }
            return false;
        });
        coordinator.request_stop();
        coordinator.request_stop();
    }
    if (name == "stop-timeout" || name == "stop-during-connect") {
        until([&] {
            return name == "stop-timeout" ? s.device->calls.load() > 0
                                          : s.device->connections.load() > 0;
        });
        auto begin = std::chrono::steady_clock::now();
        coordinator.request_stop();
        require(std::chrono::steady_clock::now() - begin < 100ms, "stop blocked on native action");
        until([&] { return coordinator.snapshot().reason == "STOP_TIMEOUT"; });
        auto timed = coordinator.snapshot();
        require(timed.state == RunState::Failed && !timed.quiescent, "timeout falsely quiescent");
        auto another = d;
        another.request_id = "request-2";
        try {
            coordinator.start(another, s.device);
            throw std::runtime_error("second run accepted");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()) == "RUN_BUSY", error.what());
        }
        runtime::RunCoordinator competitor(s.output / "competitor");
        try {
            competitor.start(another, s.device);
            throw std::runtime_error("device lease released early");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()) == "DEVICE_BUSY", error.what());
        }
        s.device->unblock = true;
    }
    if (name == "release-timeout") {
        until([&] { return s.device->release_calls.load() > 0; });
        coordinator.request_stop();
        until([&] { return coordinator.snapshot().reason == "STOP_TIMEOUT"; });
        require(!coordinator.snapshot().quiescent, "held touch considered quiescent");
        s.device->reject_release = false;
    }
    require(coordinator.wait_for(10000ms), "run did not finish");
    auto result = coordinator.snapshot();
    auto events = coordinator.events();
    require(result.quiescent, "not quiescent after completion");
    const bool stopped = name == "wait-stop" || name == "custom-stop" || name == "nested-stop" ||
                         name == "clone-stop";
    const bool failed =
        name == "callback-exception" || name == "child-failure" || name == "root-not-terminal" ||
        name == "wrong-root-node" || name == "framework-failure" || name == "builtin-unguarded" ||
        name == "postcondition-timeout" || name == "permission-denied" ||
        name == "application-mismatch" || name == "initialization-failure" ||
        name == "stop-timeout" || name == "stop-during-connect" || name == "session-time-limit" ||
        name == "release-timeout" || name == "result-save-failure" || name == "sequential-reset";
    auto expected = stopped                         ? RunState::UserStopped
                    : failed                        ? RunState::Failed
                    : name == "unresolved-recovery" ? RunState::Interrupted
                                                    : RunState::Completed;
    require(result.state == expected, std::string("unexpected outcome: ") +
                                          std::string(contracts::name(result.state)) + " / " +
                                          result.reason);
    if (name == "child-failure") {
        require(result.reason == "CHILD_FAILED", "child failure lost: " + result.reason);
        require(s.device->calls == 0, "child failed but input continued");
        bool valid_failed = false;
        for (const auto &e : events["events"])
            if (e["type"] == "child.result")
                valid_failed =
                    e["payload"]["id"].get<int64_t>() > 0 && e["payload"]["status"] == 4000;
        require(valid_failed, "valid child ID + failed status was not tested");
    }
    if (name == "root-not-terminal")
        require(result.engine_status == 3000, "root boundary not tested with engine success");
    if (name == "stop-timeout" || name == "stop-during-connect" || name == "release-timeout")
        require(result.reason == "STOP_TIMEOUT", "timeout overwritten by eventual stop");
    if (name == "held-touch" || name == "held-key")
        require(result.inputs.cleanup_called == 1, "held input not released");
    if (name == "recovery")
        require(result.generation == 2 && s.device->connections == 2,
                "recovery did not create fresh session");
    if (name == "result-save-failure")
        require(!result.result_saved && !contains(events, "run.terminal"),
                "failed write claimed persisted terminal");
    else {
        auto saved = storage::RunStore::read_summary(coordinator.run_directory());
        require(result.result_saved &&
                    saved["state"].get<std::string>() == contracts::name(result.state),
                "terminal persistence mismatch");
        require(saved["events"] == events && contains(events, "run.terminal"),
                "terminal events not atomically persisted");
    }
    if (!failed && name != "recovery")
        check_cleanup(events);
    if (name == "duplicate-start") {
        require(s.device->connections == 1, "duplicate connection");
        auto again = coordinator.start(d, s.device);
        require(again.run_id == started.run_id, "completed duplicate restarted");
    }
    if (name == "normal" || name == "large-frame") {
        require(result.inputs.backend_called == 1, "guarded action count mismatch");
        auto sent = s.device->sent.front();
        require(std::abs(sent.x - (s.device->size.width == 900 ? 374 : 449)) <= 1 &&
                    std::abs(sent.y - (s.device->size.width == 900 ? 437 : 524)) <= 1,
                "coordinate mapped incorrectly");
        require(contains(events, "postcondition.confirmed"), "no postcondition evidence");
    }
    if (name == "native-scroll") {
        require(result.inputs.backend_called == 1 &&
                    s.device->sent.front().kind == ActionKind::Scroll,
                "SDK scroll not guarded exactly once");
        bool rejected_move = false;
        for (const auto &e : events["events"])
            if (e["type"] == "input.rejected" &&
                e["payload"]["reason"] == "SCROLL_CURSOR_MOVE_NOT_AUTHORIZED")
                rejected_move = true;
        require(rejected_move, "SDK cursor move was not observed and rejected");
    }
    if (name == "sequential-reset" || name == "old-request-replay" ||
        name == "resource-isolation") {
        auto old_file = bytes(coordinator.run_directory() / "result.json");
        auto old_path = coordinator.run_directory();
        s.device->changed = false;
        auto second = s.definition();
        second.request_id = "request-2";
        if (name == "resource-isolation") {
            require(s.alternate.has_value(), "alternate resource fixture missing");
            second.initial.bundle = *s.alternate;
            second.policy.pack_revision = s.alternate->revision;
        }
        coordinator.start(second, s.device);
        require(coordinator.wait_for(10000ms), "second run did not finish");
        auto later = coordinator.snapshot();
        require(later.state ==
                        (name == "resource-isolation" ? RunState::Failed : RunState::Completed) &&
                    later.run_id == started.run_id + 1 && later.generation == 1,
                "new session retained old state");
        require(later.inputs.backend_called == (name == "resource-isolation" ? 0 : 1) &&
                    s.device->connections == 2,
                "new run duplicated inputs/history");
        if (name == "resource-isolation")
            require(later.reason == "CUSTOM_ACTION_FAILED" &&
                        !contains(coordinator.events(), "root.evidence"),
                    "same-name resource or previous root reused");
        auto replay = coordinator.start(d, s.device);
        require(replay.run_id == started.run_id && replay.state == result.state &&
                    s.device->connections == 2,
                "old request restarted or returned wrong run");
        require(bytes(old_path / "result.json") == old_file, "old terminal rewritten");
        return {{"first", storage::snapshot_json(result)},
                {"second", storage::snapshot_json(later)},
                {"replayed", storage::snapshot_json(replay)}};
    }
    return {{"snapshot", storage::snapshot_json(result)}, {"events", events}};
}
} // namespace fixture
