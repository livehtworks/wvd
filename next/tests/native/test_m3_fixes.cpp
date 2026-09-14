#include "runtime_fixture.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "loaded_modules.hpp"
#include "integrity_lease_cases.hpp"
#include <iostream>

using namespace fixture;
// 只有匹配独立测试预期的输入才能推进画面；截图本身绝不推进场景。
class CausalDevice final : public OfflineDevice {
  public:
    J expected;
    bool mismatch{};
    bool execute(const contracts::Command &c) override {
        if (changed || int(c.kind) != expected.at("kind").get<int>() ||
            c.x != expected.value("x", 0) || c.y != expected.value("y", 0)) {
            mismatch = true;
            return false;
        }
        return OfflineDevice::execute(c);
    }
};
int main(int argc, char **argv) {
    try {
        require(argc == 2, "CONFIG_REQUIRED");
        J config;
        std::ifstream(maafw::path_from_utf8(argv[1])) >> config;
        maafw::Bundle bundle{maafw::path_from_utf8(config.at("bundle")), "fixes-fixture-1", {}};
        for (const auto &f : config.at("files"))
            bundle.files.push_back({f.at("path"), f.at("sha256")});
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m3-fixes-1");
        games::vision::register_wvd(*registry);
        registry->seal();
        auto device = std::make_shared<CausalDevice>();
        device->before = bytes(maafw::path_from_utf8(config.at("before")));
        device->after = bytes(maafw::path_from_utf8(config.at("after")));
        device->expected = config.value("expected_input", J::object());
        device->change_frame = config.value("change_frame", true);
        contracts::InputPolicy policy{
            "m2-offline",
            "wvd",
            "fixture.app",
            bundle.revision,
            "portrait",
            {900, 1600},
            {contracts::ActionKind::Click, contracts::ActionKind::ClickKey},
            {contracts::ActionKind::Click, contracts::ActionKind::ClickKey},
            {"battle"},
            2000ms};
        J output;
        if (config.at("mode") == "lease-matrix") {
            output = integrity_lease_case(bundle, config.at("scenario"));
            output["backend_calls"] = device->calls.load();
        } else if (config.at("mode") == "guarded") {
            runtime::RunCoordinator coordinator(maafw::path_from_utf8(config.at("run_root")),
                                                registry);
            runtime::RunDefinition definition;
            definition.request_id = "guarded";
            definition.policy = policy;
            definition.initial = {bundle, "Entry", "Terminal", {}, 5000ms, 150ms};
            definition.initial.recognitions = {games::vision::binding(J::object())};
            coordinator.start(definition, device);
            until(
                [&] {
                    auto s = coordinator.snapshot();
                    return s.quiescent && (s.result_saved || !s.storage_error.empty());
                },
                7000ms);
            output = {{"snapshot", storage::snapshot_json(coordinator.snapshot())},
                      {"backend_calls", device->calls.load()},
                      {"mismatch", device->mismatch}};
        } else if (config.at("mode") == "integrity") {
            storage::EventJournal events("integrity", 1);
            devices::InputGate gate(*device, policy, 1, 1, events);
            maafw::MaaGateway gateway(
                bundle, &gate, {}, {},
                registry->bind_recognitions({games::vision::binding(J::object())}));
            gateway.initialize();
            output["active"] = gateway.bundle_status();
            auto root = maafw::path_from_utf8(output["active"].at("root"));
            auto file = root / "image/target.png";
            {
                std::ofstream write(file, std::ios::binary);
                output["write_blocked"] = !write;
            }
            std::error_code error;
            std::filesystem::remove(file, error);
            output["delete_blocked"] = bool(error);
            std::filesystem::rename(file, root / "image/renamed.png", error);
            output["replace_blocked"] = bool(error);
            auto frame = gateway.capture();
            auto request = maafw::parse_recognition_request(config.at("request"));
            auto before = gateway.bundle_status();
            auto matched = gateway.recognize(frame, gate.frame_identity(), request);
            output["matched"] = int(matched.outcome);
            output["detail"] = matched.evidence;
            output["one_boundary"] = gateway.bundle_status().at("directory_checks").get<int>() -
                                         before.at("directory_checks").get<int>() ==
                                     1;
            // 源目录改动不影响活动快照；这是被指定的副本语义，不是旧缓存忽略活动文件修改。
            {
                std::ofstream author(bundle.root / "image/target.png", std::ios::binary);
                author << "changed author";
            }
            output["author_change_ignored"] =
                int(gateway.recognize(frame, gate.frame_identity(), request).outcome) == 0;
            {
                std::ofstream extra(root / "image/extra.png", std::ios::binary);
                extra << "extra";
            }
            auto changed = gateway.recognize(frame, gate.frame_identity(), request);
            output["member_change_error"] = changed.error_code;
            gate.close();
            gateway.close();
            gate.disconnect_backend();
            output["closed"] = gateway.bundle_status();
            {
                std::ofstream released(file, std::ios::binary);
                released << "changed after release";
                released.close();
                output["released_write_succeeded"] = bool(released);
            }
            maafw::OfflineRecognizer stale(bundle);
            output["old_manifest_error"] =
                stale.evaluate(frame, frame.identity, request).error_code;
            auto revised = bundle;
            for (auto &member : revised.files)
                member.sha256 = platform::file_sha256(revised.root /
                                                      maafw::path_from_utf8(member.relative_path));
            maafw::OfflineRecognizer reused_revision(revised);
            output["reused_revision_error"] =
                reused_revision.evaluate(frame, frame.identity, request).error_code;
            output["backend_calls"] = device->calls.load();
        } else {
            storage::EventJournal events("fixes", 1);
            devices::InputGate gate(*device, policy, 1, 1, events);
            J observed = J::array();
            maafw::GatewayHooks hooks;
            hooks.event = [&](const std::string &name, const J &data) {
                if (name == "recognition.custom")
                    observed.push_back(data);
            };
            maafw::MaaGateway gateway(
                bundle, &gate, hooks, {},
                registry->bind_recognitions({games::vision::binding(J::object())}));
            gateway.initialize();
            auto frame = gateway.capture();
            output["cases"] = J::array();
            for (const auto &test : config.at("cases")) {
                auto result =
                    gateway.recognize(frame, gate.frame_identity(),
                                      maafw::parse_recognition_request(test.at("request")));
                output["cases"].push_back({{"id", test.at("id")},
                                           {"outcome", int(result.outcome)},
                                           {"error", result.error_code},
                                           {"evidence", result.evidence},
                                           {"center", result.center.has_value()}});
            }
            for (const auto &test : config.at("cases")) {
                observed = J::array();
                auto id = gateway.post(test.at("id"));
                until([&] {
                    auto s = gateway.status(id);
                    return s != MaaStatus_Pending && s != MaaStatus_Running;
                });
                output["pipelines"].push_back(
                    {{"id", test.at("id")}, {"events", observed}, {"status", gateway.status(id)}});
            }
            gate.close();
            gateway.close();
            gate.disconnect_backend();
            output["backend_calls"] = device->calls.load();
        }
        output["loaded_modules"] = loaded_vision_modules();
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
