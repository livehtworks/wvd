#include "runtime_fixture.hpp"

namespace fixture {
J gate_case(const std::string &name, Setup &s) {
    using namespace contracts;
    auto policy = s.policy;
    if (name == "gate-permission")
        policy.permissions.clear();
    if (name == "gate-package")
        s.device->application = "wrong.app";
    storage::EventJournal journal(platform::unique_id(), 1);
    devices::InputGate gate(*s.device, policy, 1, 1, journal);
    maafw::MaaGateway gateway(s.bundle, &gate);
    gateway.initialize();
    if (name == "gate-reinitialize") {
        gateway.close();
        try {
            gateway.initialize();
            throw std::runtime_error("closed gateway reused");
        } catch (const std::runtime_error &e) {
            require(std::string(e.what()) == "GATEWAY_ALREADY_INITIALIZED", e.what());
        }
        require(s.device->connections == 1, "gateway connected twice");
        return {{"connections", s.device->connections.load()}};
    }
    auto frame = gateway.capture();
    auto scene_observation = gateway.recognize(frame, gate.frame_identity(), scene());
    require(scene_observation.outcome == RecognitionOutcome::Hit,
            "scene not recognized by native SDK");
    require(gate.current_observation(scene_observation) == (name != "gate-package"),
            "current observation did not enforce application identity");
    gate.confirm_scene(scene_observation, "battle");
    auto request = target();
    if (name == "gate-nohit")
        std::get<maafw::TemplateParameters>(request.parameters).image = "never.png";
    if (name == "gate-error")
        std::get<maafw::TemplateParameters>(request.parameters).image = "missing.png";
    auto observation = gateway.recognize(frame, gate.frame_identity(), request);
    if (name != "gate-nohit" && name != "gate-error")
        require(observation.outcome == RecognitionOutcome::Hit,
                "target not recognized by native SDK");
    Command input;
    input.kind = ActionKind::Click;
    input.x = 374;
    input.y = 437;
    ActionIntent intent{1, 1, 1, observation, input, "battle", "postcondition", {0, 0, 900, 900}};
    if (name.starts_with("gate-edges-")) {
        s.device->change_frame = false;
        J sent = J::array();
        auto dispatch = [&](Command command, bool valid = true, Box area = {0, 0, 900, 1600}) {
            auto fresh = gateway.capture();
            auto observed = gateway.recognize(fresh, gate.frame_identity(), scene());
            require(observed.outcome == RecognitionOutcome::Hit, "mapping fixture scene missing");
            gate.confirm_scene(observed, "battle");
            gate.authorize({1, 1, 1, observed, command, "battle", "mapping-only", area});
            auto before = s.device->calls.load();
            // 直接检查门禁后的真实记录，避免 SDK 自身的手势展开混淆端点映射断言。
            require(gate.execute(command) == valid, "edge dispatch outcome");
            if (!valid) {
                require(s.device->calls == before, "invalid baseline was clamped into screen");
                return;
            }
            auto raw = s.device->sent.back();
            sent.push_back({{"kind", int(raw.kind)},
                            {"x", raw.x},
                            {"y", raw.y},
                            {"x2", raw.x2},
                            {"y2", raw.y2}});
            if (command.kind == ActionKind::Click || command.kind == ActionKind::Swipe ||
                command.kind == ActionKind::TouchDown || command.kind == ActionKind::TouchMove) {
                require(raw.x >= 0 && raw.x < s.device->size.width && raw.y >= 0 &&
                            raw.y < s.device->size.height,
                        "raw edge out of bounds");
                if (command.x == 0)
                    require(raw.x == 0, "left edge changed");
                if (command.y == 0)
                    require(raw.y == 0, "top edge changed");
                if (command.x == 899)
                    require(raw.x == s.device->size.width - 1, "right edge changed");
                if (command.y == 1599)
                    require(raw.y == s.device->size.height - 1, "bottom edge changed");
                if (command.kind == ActionKind::Swipe)
                    require(raw.x2 >= 0 && raw.x2 < s.device->size.width && raw.y2 >= 0 &&
                                raw.y2 < s.device->size.height,
                            "swipe end outside raw image");
            } else
                require(raw == command, "delta/key/text/release was scaled");
        };
        for (auto [x, y] : std::vector<std::pair<int, int>>{
                 {0, 0}, {899, 0}, {0, 1599}, {899, 1599}, {1, 1}, {898, 1598}}) {
            Command c;
            c.x = x;
            c.y = y;
            c.x2 = 899 - x;
            c.y2 = 1599 - y;
            c.duration = 20;
            for (auto kind : {ActionKind::Click, ActionKind::Swipe, ActionKind::TouchDown,
                              ActionKind::TouchMove}) {
                c.kind = kind;
                dispatch(c);
            }
            c.kind = ActionKind::TouchUp;
            dispatch(c);
        }
        for (auto [x, y] :
             std::vector<std::pair<int, int>>{{-1, 10}, {10, -1}, {900, 10}, {10, 1600}}) {
            Command c;
            c.x = x;
            c.y = y;
            for (auto kind : {ActionKind::Click, ActionKind::Swipe, ActionKind::TouchDown,
                              ActionKind::TouchMove}) {
                c.kind = kind;
                dispatch(c, false);
            }
            c.kind = ActionKind::Swipe;
            c.x2 = x;
            c.y2 = y;
            c.x = 10;
            c.y = 10;
            dispatch(c, false);
        }
        Command c;
        c.x = 899;
        c.y = 1599;
        dispatch(c, false, {0, 0, 899, 1599});
        for (auto kind : {ActionKind::Scroll, ActionKind::RelativeMove, ActionKind::ClickKey,
                          ActionKind::Text}) {
            c.kind = kind;
            c.x = -20;
            c.y = 120;
            c.text = "test";
            c.key = 32;
            dispatch(c);
        }
        gate.close();
        gateway.close();
        return {
            {"raw_width", s.device->size.width}, {"commands", sent}, {"events", journal.read()}};
    }
    if (name == "gate-bare-all") {
        J routed = J::array();
        auto initial = gate.counts();
        for (int i = 0; i <= int(ActionKind::Inactive); ++i) {
            Command command;
            command.kind = static_cast<ActionKind>(i);
            command.x = 374;
            command.y = 437;
            command.x2 = 470;
            command.y2 = 500;
            command.duration = 30;
            command.pressure = 500;
            command.key = 32;
            command.text = "fixture.app";
            auto before = gate.counts().attempted;
            require(!gateway.controller_action(command), "bare input succeeded");
            routed.push_back({{"kind", i}, {"callbacks", gate.counts().attempted - before}});
        }
        auto counts = gate.counts();
        for (const auto &route : routed)
            require(route["callbacks"] == (route["kind"] == 9 ? 2 : 1),
                    "unexpected SDK input expansion: " + routed.dump());
        require(counts.attempted - initial.attempted == 16 &&
                    counts.rejected - initial.rejected == 16 && counts.accepted == 0 &&
                    counts.backend_called == 0,
                "not all 15 controller inputs reached observable gate: " + routed.dump());
    } else if (name == "gate-generation" || name == "gate-viewport") {
        gateway.close();
        if (name == "gate-viewport") {
            policy.viewport_id = "portrait-new";
            s.device->viewport = policy.viewport_id;
        }
        devices::InputGate changed(*s.device, policy, 1, name == "gate-generation" ? 2 : 1,
                                   journal);
        maafw::MaaGateway next(s.bundle, &changed);
        next.initialize();
        auto fresh = next.capture();
        auto observed = next.recognize(fresh, changed.frame_identity(), scene());
        changed.confirm_scene(observed, "battle");
        require(!changed.current_observation(observation), "old observation remained current");
        changed.authorize(intent);
        // SDK 识别结束会请求 Inactive；它同样被门禁拒绝，单独核对本次迟到输入的增量。
        auto before = changed.counts();
        require(!next.controller_action(input), "old real observation accepted by new context");
        auto counts = changed.counts();
        require(counts.attempted - before.attempted == 1 &&
                    counts.rejected - before.rejected == 1 && counts.accepted == 0 &&
                    s.device->calls == 0,
                "old observation bypassed gate: " + journal.read().dump());
        next.close();
        return {{"old_reco_id", observation.engine_reco_id},
                {"new_reco_id", observed.engine_reco_id},
                {"events", journal.read()}};
    } else {
        if (name == "gate-epoch") {
            gate.authorize(intent);
            require(gateway.controller_action(input), "first action failed");
            auto fresh = gateway.capture();
            auto observed = gateway.recognize(fresh, gate.frame_identity(), scene());
            gate.confirm_scene(observed, "battle");
            require(!gate.current_observation(observation), "old frame remained current");
        }
        if (name == "gate-bounds") {
            input.x = 901;
            intent.command = input;
        }
        if (name == "gate-held-release") {
            input.kind = ActionKind::TouchDown;
            input.pressure = 500;
            intent.command = input;
        }
        gate.authorize(intent);
        if (name == "gate-late")
            gate.close();
        if (name == "gate-expired")
            std::this_thread::sleep_until(gate.frame_identity().captured_at + policy.max_frame_age +
                                          10ms);
        if (name == "gate-expired" || name == "gate-late")
            require(!gate.current_observation(observation), "expired/closed observation accepted");
        auto success = gateway.controller_action(input);
        if (name == "gate-normal" || name == "gate-held-release")
            require(success, "authorized input failed");
        else
            require(!success, "invalid input accepted");
        if (name == "gate-held-release") {
            gate.close();
            require(!gate.quiescent(), "held touch falsely quiescent");
            require(gate.release_held(), "touch cleanup failed");
            require(gate.counts().cleanup_called == 1 && s.device->release_calls == 1,
                    "cleanup missing");
            require(gate.release_held() && s.device->release_calls == 1, "cleanup not idempotent");
        }
        auto counts = gate.counts();
        require(counts.attempted == counts.accepted + counts.rejected,
                "attempt accounting mismatch");
        if (name != "gate-normal" && name != "gate-held-release" && name != "gate-epoch")
            require(s.device->calls == 0, "invalid input reached backend");
    }
    gate.close();
    gateway.close();
    auto counts = gate.counts();
    return {{"reco_id", observation.engine_reco_id},
            {"attempted", counts.attempted},
            {"accepted", counts.accepted},
            {"rejected", counts.rejected},
            {"backend_called", counts.backend_called},
            {"events", journal.read()}};
}
J storage_case(const std::string &name, Setup &s) {
    if (name == "store-terminal-transaction") {
        storage::EventJournal journal("test-instance", 1, 8);
        for (int i = 0; i < 8; ++i)
            journal.emit(1, "critical", {}, true);
        auto before = journal.read();
        try {
            journal.commit_terminal(1, {{"state", "Completed"}}, [&](const J &) {
                require(journal.read() == before, "uncommitted journal changed");
                throw std::runtime_error("WRITE_FAILED");
            });
        } catch (const std::runtime_error &e) {
            require(std::string(e.what()) == "WRITE_FAILED", e.what());
        }
        require(journal.read() == before, "failed commit published terminal");
        J persisted;
        journal.commit_terminal(1, {{"state", "Completed"}},
                                [&](const J &events) { persisted = events; });
        require(persisted == journal.read() && persisted["events"].size() == 9 &&
                    contains(persisted, "run.terminal"),
                "terminal reservation / atomic publication failed");
        try {
            journal.commit_terminal(1, {}, [](const J &) {});
            throw std::runtime_error("duplicate terminal allowed");
        } catch (const std::runtime_error &e) {
            require(std::string(e.what()) == "JOURNAL_ALREADY_TERMINAL", e.what());
        }
        return persisted;
    }
    if (name == "store-events") {
        storage::EventJournal journal("test-instance", 1, 8);
        journal.emit(1, "stop", {}, true);
        for (int i = 0; i < 50; ++i)
            journal.emit(1, "ordinary", {{"value", i}});
        journal.emit(1, "terminal", {}, true);
        auto result = journal.read();
        for (const auto &e : result["events"])
            require(e.contains("server_instance_id") && e.contains("run_id") &&
                        e.contains("session_generation") && e.contains("seq") &&
                        e.contains("monotonic_time") && e.contains("node_id") &&
                        e.contains("outcome") && e.contains("payload"),
                    "event contract incomplete");
        require(result["events"].size() == 8 && result["resync_required"],
                "journal not bounded / missing gap");
        require(contains(result, "stop") && contains(result, "terminal"), "critical event lost");
        require(!journal.read(result["last_seq"])["resync_required"].get<bool>(),
                "cursor cannot recover");
        return result;
    }
    if (name == "store-critical-overflow") {
        storage::EventJournal journal("test-instance", 1, 8);
        for (int i = 0; i < 8; ++i)
            journal.emit(1, "critical", {}, true);
        try {
            journal.emit(1, "overflow", {}, true);
            throw std::runtime_error("overflow silently ignored");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()) == "CRITICAL_EVENT_CAPACITY_EXCEEDED", error.what());
        }
        return journal.read();
    }
    storage::RunStore store(s.output, platform::unique_id(), 1, {{"scope", "test-only"}});
    if (name == "store-interrupted") {
        auto summary = storage::RunStore::read_summary(store.directory());
        require(summary["state"] == "Interrupted" && !summary["resume_allowed"].get<bool>(),
                "uncommitted run resumed");
        return summary;
    }
    contracts::RunSnapshot snapshot;
    snapshot.state = contracts::RunState::Completed;
    snapshot.quiescent = true;
    contracts::SessionResult session;
    store.save_terminal(snapshot, session);
    auto before = bytes(store.directory() / "result.json");
    try {
        store.save_terminal(snapshot, session);
        throw std::runtime_error("duplicate terminal saved");
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()) == "TERMINAL_ALREADY_SAVED", error.what());
    }
    require(before == bytes(store.directory() / "result.json"), "terminal overwritten");
    return storage::RunStore::read_summary(store.directory());
}
} // namespace fixture
