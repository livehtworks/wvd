#include "runtime_fixture.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "games/wvd/vision/unknown_window.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "loaded_modules.hpp"
#include "integrity_lease_cases.hpp"
#include "directory_bundle_cases.hpp"
#include <iostream>
#include <cmath>
#include <opencv2/imgproc.hpp>

using namespace fixture;
// 只有匹配独立测试预期的输入才能推进画面；截图本身绝不推进场景。
class CausalDevice final : public OfflineDevice {
  public:
    J expected;
    bool mismatch{};
    std::function<void()> capture_fault;
    devices::RawFrame capture() override {
        if (capture_fault)
            capture_fault();
        return OfflineDevice::capture();
    }
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
        if (config.at("mode") == "directory-bundle") {
            output = directory_bundle_case(bundle, config, registry, device, policy);
        } else if (config.at("mode") == "unknown-window") {
            games::vision::UnknownWindow window;
            cv::Mat previous;
            std::vector<double> reference;
            output["samples"] = J::array();
            const auto start = std::chrono::steady_clock::time_point{};
            for (unsigned i = 0; i < 20; ++i) {
                // 首十帧静止；后十帧交替亮暗，参考算法独立保存逐帧差值。
                cv::Mat image(1600, 900, CV_8UC3, cv::Scalar::all(i < 10 ? 30 : (i % 2 ? 220 : 10)));
                const auto now = start + std::chrono::seconds{i};
                const auto actual = window.observe(image, now);
                require(actual.samples == i + 1, "UNKNOWN_SAMPLE_COUNT_INVALID");
                const auto read_only = window.latest();
                require(read_only.samples == actual.samples && !read_only.sampled && !read_only.evaluated,
                        "UNKNOWN_READ_ADVANCED_SAMPLE");
                cv::Mat gray, difference;
                cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
                if (!previous.empty()) {
                    cv::absdiff(gray, previous, difference);
                    reference.push_back(cv::mean(difference)[0] / 255);
                }
                previous = gray.clone();
                double sum = 0;
                for (std::size_t j = reference.size() > 9 ? reference.size() - 9 : 0; j < reference.size(); ++j)
                    sum += reference[j];
                require(actual.sampled && std::abs(actual.total_difference - sum) < 1e-10,
                    "UNKNOWN_WINDOW_REFERENCE_MISMATCH");
                const auto duplicate = window.observe(image, now + 10ms);
                require(!duplicate.sampled && duplicate.window_size == actual.window_size,
                    "UNKNOWN_FAST_FRAME_COUNTED_TWICE");
                output["samples"].push_back({{"size", actual.window_size}, {"evaluated", actual.evaluated},
                    {"frozen", actual.frozen}, {"difference", actual.total_difference}});
            }
            window.clear();
            cv::Mat still(1600, 900, CV_8UC3, cv::Scalar::all(30));
            const auto reset = window.observe(still, start + 21s);
            require(reset.window_size == 1 && !reset.frozen, "UNKNOWN_WINDOW_NOT_RESET");
            output["reset_size"] = reset.window_size;
            for (const auto &kind : {std::string("invalid_frame"), std::string("reversed_clock")}) {
                try {
                    window.observe(kind == "invalid_frame" ? cv::Mat{} : still, start + 20s);
                    output[kind] = "";
                } catch (const std::exception &e) {
                    output[kind] = e.what();
                }
            }
            output["backend_calls"] = device->calls.load();
        } else if (config.at("mode") == "lease-matrix") {
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
            std::mutex evidence_mutex;
            J failures = J::array();
            std::atomic<unsigned> reached{};
            maafw::GatewayHooks hooks;
            hooks.failure = [&](const std::string &code) {
                std::lock_guard lock(evidence_mutex);
                failures.push_back(code);
            };
            maafw::ActionRegistry actions{{"RecordReached", [&](maafw::Context &, const J &) {
                ++reached;
                return true;
            }}};
            maafw::MaaGateway gateway(
                bundle, &gate, hooks, std::move(actions),
                registry->bind_recognitions({games::vision::binding(J::object())}));
            gateway.initialize();
            // 第二个入口只有 Resource，无 Controller；它也必须封存独立资源副本。
            maafw::OfflineRecognizer offline(bundle);
            output["active"] = gateway.bundle_status();
            auto root = maafw::path_from_utf8(output["active"].at("root"));
            auto file = root / "image/target.png";
            output["protected_files"] = J::object();
            for (const auto &relative : config.value("protected_paths", std::vector<std::string>{})) {
                std::ofstream write(root / maafw::path_from_utf8(relative), std::ios::binary);
                output["protected_files"][relative] = !write;
            }
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
            for (const auto &relative : config.value("protected_paths", std::vector<std::string>{})) {
                std::ofstream author(bundle.root / maafw::path_from_utf8(relative), std::ios::binary);
                author << "changed author before first native recognition";
                author.close();
                require(bool(author), "TEST_AUTHOR_WRITE_FAILED");
            }
            const auto native_request = maafw::parse_recognition_request(config.at("native_request"));
            // SDK 模板的首次实际识别发生在作者文件改变以后，不借前一次命中暖缓存。
            const auto sdk_checks_before = gateway.bundle_status().at("directory_checks").get<int>();
            output["delayed_sdk_direct"] = int(gateway.recognize(frame, gate.frame_identity(), native_request).outcome);
            output["sdk_one_boundary"] = gateway.bundle_status().at("directory_checks").get<int>() - sdk_checks_before == 1;
            const auto offline_checks_before = offline.bundle_status().at("directory_checks").get<int>();
            output["delayed_offline"] = int(offline.evaluate(frame, frame.identity, native_request).outcome);
            output["offline_one_boundary"] = offline.bundle_status().at("directory_checks").get<int>() - offline_checks_before == 1;
            auto pipeline = [&](const std::string &entry) {
                const auto before = reached.load();
                {
                    std::lock_guard lock(evidence_mutex);
                    failures = J::array();
                }
                J result;
                try {
                    const auto task = gateway.post(entry);
                    until([&] {
                        const auto status = gateway.status(task);
                        return status != MaaStatus_Pending && status != MaaStatus_Running &&
                               gateway.active_callbacks() == 0;
                    });
                    result["status"] = gateway.status(task);
                } catch (const std::exception &e) {
                    result["error"] = e.what();
                }
                result["reached"] = reached.load() - before;
                std::lock_guard lock(evidence_mutex);
                result["failures"] = failures;
                return result;
            };
            output["delayed_sdk_pipeline"] = pipeline("SdkEntry");
            output["delayed_custom_pipeline"] = pipeline("CustomEntry");
            frame = gateway.capture();
            {
                std::ofstream extra(root / "image/extra.png", std::ios::binary);
                extra << "extra";
            }
            auto changed = gateway.recognize(frame, gate.frame_identity(), request);
            output["member_change_error"] = changed.error_code;
            output["sdk_pipeline_changed"] = pipeline("SdkEntry");
            output["custom_pipeline_changed"] = pipeline("CustomEntry");
            // 原生开始事件失败会永久关闭本 Gateway；放在其余独立入口断言之后。
            output["sdk_member_change_error"] = gateway.recognize(frame, gate.frame_identity(), native_request).error_code;
            const auto offline_root = maafw::path_from_utf8(offline.bundle_status().at("root"));
            {
                std::ofstream extra(offline_root / "image/extra.png", std::ios::binary);
                extra << "extra";
            }
            output["offline_member_change_error"] = offline.evaluate(frame, frame.identity, native_request).error_code;
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
        } else if (config.at("mode") == "integrity-mid-call") {
            storage::EventJournal events("integrity-mid-call", 1);
            devices::InputGate gate(*device, policy, 1, 1, events);
            std::atomic<unsigned> reached{};
            std::mutex evidence_mutex;
            J failures = J::array();
            maafw::GatewayHooks hooks;
            hooks.failure = [&](const std::string &reason) {
                std::lock_guard lock(evidence_mutex);
                failures.push_back(reason);
            };
            maafw::MaaGateway gateway(bundle, &gate, hooks,
                {{"RecordReached", [&](maafw::Context &, const J &) { ++reached; return true; }}});
            gateway.initialize();
            const auto active = maafw::path_from_utf8(gateway.bundle_status().at("root"));
            bool mutated = false;
            // 明确的文件系统故障发生在 post 的预检之后、原生识别之前，截图内容不变。
            device->capture_fault = [&] {
                if (!mutated) {
                    std::ofstream extra(active / "image/unexpected.png", std::ios::binary);
                    extra << "added between submit and recognition";
                    extra.close();
                    require(bool(extra), "TEST_MEMBER_WRITE_FAILED");
                    mutated = true;
                }
            };
            const auto task = gateway.post("SdkEntry");
            until([&] {
                const auto status = gateway.status(task);
                return status != MaaStatus_Pending && status != MaaStatus_Running && gateway.active_callbacks() == 0;
            });
            output = {{"reached", reached.load()}, {"status", gateway.status(task)},
                      {"mutated", mutated}, {"gate_closed", gate.closed()}, {"backend_calls", device->calls.load()}};
            {
                std::lock_guard lock(evidence_mutex);
                output["failures"] = failures;
            }
            gate.close();
            gateway.close();
            gate.disconnect_backend();
        } else if (config.at("mode") == "cross-bundle") {
            maafw::Bundle other{maafw::path_from_utf8(config.at("other_bundle")), config.at("other_revision"), {}};
            for (const auto &f : config.at("other_files"))
                other.files.push_back({f.at("path"), f.at("sha256")});
            // 无 Controller 的实际 Gateway/OfflineRecognizer；同名资源不能串包或版本。
            maafw::MaaGateway first(bundle, nullptr, {}, {},
                registry->bind_recognitions({games::vision::binding(J::object())}));
            maafw::MaaGateway second(other, nullptr, {}, {},
                registry->bind_recognitions({games::vision::binding(J::object())}));
            first.initialize();
            second.initialize();
            maafw::OfflineRecognizer offline_first(bundle), offline_second(other);
            const auto sdk = maafw::parse_recognition_request(config.at("native_request"));
            const auto custom = maafw::parse_recognition_request(config.at("request"));
            output["observations"] = J::array();
            for (const auto index : {0, 1, 0}) {
                auto &gateway = index == 0 ? first : second;
                auto &offline = index == 0 ? offline_first : offline_second;
                const auto &selected = index == 0 ? bundle : other;
                for (unsigned image_index = 0; image_index < 2; ++image_index) {
                    contracts::FrameIdentity identity{"m2-offline", "wvd", selected.revision, "portrait",
                        1, image_index + 1, 0, {900, 1600}, {900, 1600}, std::chrono::steady_clock::now(), "BGR8"};
                    contracts::FrameEnvelope frame{identity, image_index == 0 ? device->before : device->after};
                    const auto a = gateway.recognize(frame, identity, sdk);
                    const auto b = gateway.recognize(frame, identity, custom);
                    const auto c = offline.evaluate(frame, identity, sdk);
                    output["observations"].push_back({{"bundle", index}, {"image", image_index},
                        {"sdk", int(a.outcome)}, {"custom", int(b.outcome)}, {"offline", int(c.outcome)},
                        {"errors", J::array({a.error_code, b.error_code, c.error_code})}});
                }
            }
            first.close();
            second.close();
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
