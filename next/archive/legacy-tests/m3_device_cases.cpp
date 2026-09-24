#include "devices/screenshot_route.hpp"
#include "runtime_fixture.hpp"
#include <opencv2/imgcodecs.hpp>

namespace fixture {
J m3_device_cases(const J &config) {
    J outcomes = J::array();
    for (auto mode : {"primary", "connect-fallback", "capture-fallback", "double-failure"}) {
        devices::ScreenshotRoute route;
        std::vector<bool> opened;
        bool current = false;
        int live = 0, max_live = 0;
        auto open = [&](bool encode) {
            live = 0;
            current = encode;
            opened.push_back(encode);
            ++live;
            max_live = std::max(live, max_live);
            return std::string_view(mode) != "connect-fallback" || encode;
        };
        require(route.connect(open), "route connection failed");
        auto capture = [&]() -> int {
            if (std::string_view(mode) == "double-failure" ||
                (!current && std::string_view(mode) == "capture-fallback"))
                throw std::runtime_error("INJECTED_CAPTURE_ERROR");
            return current ? 2 : 1;
        };
        bool failed = false;
        int image = 0;
        try {
            image = route.capture(capture, open);
        } catch (const std::runtime_error &) {
            failed = true;
        }
        require(failed == (std::string_view(mode) == "double-failure"),
                "fallback error folded into frame");
        if (!failed) {
            require(image == (std::string_view(mode) == "primary" ? 1 : 2), "wrong actual backend");
            auto count = opened.size();
            route.capture(capture, open);
            require(opened.size() == count, "primary retried every frame");
        }
        require(max_live == 1, "parallel capture controllers");
        outcomes.push_back({{"case", mode},
                            {"outcome", "PASS"},
                            {"fault_injection", std::string_view(mode) != "primary"},
                            {"open_sequence", opened},
                            {"failures", route.failures()}});
    }
    Setup s(config);
    class ObservedDevice final : public OfflineDevice {
      public:
        bool fail_capture = false;
        bool verified_access() const override {
            return true;
        }
        bool context_matches(const contracts::FrameIdentity &frame,
                             const std::string &app) override {
            return size == frame.raw_size && application == app;
        }
        devices::RawFrame capture() override {
            if (fail_capture)
                throw std::runtime_error("INJECTED_CAPTURE_ERROR");
            return OfflineDevice::capture();
        }
    };
    auto device = std::make_shared<ObservedDevice>();
    device->before = s.device->before;
    device->after = device->before;
    device->identity = s.device->identity;
    device->real = true;
    storage::EventJournal journal("m3-gate", 1);
    devices::InputGate gate(*device, s.policy, 1, 1, journal);
    maafw::MaaGateway gateway(s.bundle, &gate);
    gateway.initialize();
    auto frame = gateway.capture();
    auto observation = gateway.recognize(frame, gate.frame_identity(), scene());
    require(observation.outcome == contracts::RecognitionOutcome::Hit, "M3 native scene missing");
    gate.confirm_scene(observation, "battle");
    contracts::Command command;
    command.x = 374;
    command.y = 437;
    gate.authorize({1, 1, 1, observation, command, "battle", "post", {0, 0, 900, 1600}});
    device->application = "changed.app";
    require(!gateway.controller_action(command) && device->calls == 0 &&
                gate.frame_identity().frame_id == 0,
            "foreground change not revoked before input");
    device->application = "fixture.app";
    gateway.capture();
    device->fail_capture = true;
    try {
        gateway.capture();
        throw std::runtime_error("capture fault accepted");
    } catch (const std::runtime_error &e) {
        require(std::string(e.what()) == "CAPTURE_FAILED", e.what());
    }
    require(gate.frame_identity().frame_id == 0, "failed capture retained old frame");
    gate.authorize({1, 1, 1, observation, command, "battle", "post", {0, 0, 900, 1600}});
    require(!gateway.controller_action(command) && device->calls == 0,
            "late input after failed capture");
    device->fail_capture = false;
    device->before = {1, 2, 3};
    try {
        gateway.capture();
        throw std::runtime_error("bad pixels accepted");
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()) == "CAPTURE_FAILED", error.what());
    }
    require(gate.frame_identity().frame_id == 0, "decode failure kept valid identity");
    gate.close();
    gateway.close();
    gate.disconnect_backend();
    outcomes.push_back({{"case", "native-gate-context-and-capture-failure"},
                        {"outcome", "PASS"},
                        {"backend_inputs", device->calls.load()},
                        {"events", journal.read()}});
    {
        auto observer = std::make_shared<OfflineDevice>();
        observer->before = s.device->before;
        observer->after = observer->before;
        observer->change_frame = false;
        auto policy = s.policy;
        policy.observed_read_only_viewport = true;
        bool rejected = false;
        try {
            devices::InputGate invalid(*observer, policy, 2, 1, journal);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()) == "READ_ONLY_VIEWPORT_WITH_INPUT_POLICY";
        }
        require(rejected && observer->connections == 0, "observer with permissions connected");
        policy.permissions.clear();
        policy.capabilities.clear();
        policy.allowed_scenes.clear();
        storage::EventJournal events("m3-system-viewports", 2);
        devices::InputGate observe_gate(*observer, policy, 2, 1, events);
        maafw::MaaGateway observe_gateway(s.bundle, &observe_gate);
        observe_gateway.initialize();
        const auto portrait = observe_gateway.capture();
        auto pixels = cv::imdecode(observer->before, cv::IMREAD_COLOR);
        cv::rotate(pixels, pixels, cv::ROTATE_90_CLOCKWISE);
        cv::imencode(".png", pixels, observer->before);
        observer->size = {1600, 900};
        observer->viewport = "system-landscape";
        const auto landscape = observe_gateway.capture();
        require(landscape.identity.raw_size == contracts::Size{1600, 900} &&
                    landscape.identity.recognition_size == landscape.identity.raw_size,
                "system viewport forced to game ratio");
        auto actual = cv::imdecode(landscape.encoded_image, cv::IMREAD_COLOR);
        require(actual.cols == 1600 && actual.rows == 900 &&
                    cv::norm(actual, pixels, cv::NORM_INF) == 0,
                "system pixels stretched or rotated");
        require(!observe_gateway.controller_action(command) && observer->calls == 0,
                "read-only system view allowed input");
        auto stale = observe_gateway.recognize(portrait, observe_gate.frame_identity(), scene());
        require(stale.outcome == contracts::RecognitionOutcome::Error, "old viewport accepted");
        observe_gate.close();
        observe_gateway.close();
        observe_gate.disconnect_backend();
        outcomes.push_back({{"case", "system-observed-viewport"},
                            {"outcome", "PASS"},
                            {"backend_inputs", observer->calls.load()},
                            {"events", events.read()}});
    }
    return outcomes;
}
} // namespace fixture
