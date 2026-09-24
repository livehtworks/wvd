#include "app/application.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <thread>

namespace {
using J = nlohmann::json;
using namespace std::chrono_literals;

class OfflineConnection final : public wvd::devices::DeviceConnection,
                                public wvd::devices::LifecyclePort {
  public:
    explicit OfflineConnection(const std::filesystem::path &pack) {
        auto marker = cv::imread((pack / "image/Inn.png").string(), cv::IMREAD_COLOR);
        if (marker.empty() || marker.cols > 900 || marker.rows > 1600)
            throw std::runtime_error("OFFLINE_MARKER_INVALID");
        cv::Mat frame(1600, 900, CV_8UC3, cv::Scalar(0, 0, 0));
        marker.copyTo(frame(cv::Rect(60, 450, marker.cols, marker.rows)));
        pixels_.assign(frame.data, frame.data + frame.total() * frame.elemSize());
    }
    bool offline() const override { return true; }
    bool verified_access() const override { return true; }
    bool connect() override { return true; }
    wvd::devices::RawFrame capture() override {
        wvd::devices::RawFrame frame;
        frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(pixels_);
        frame.size = {900, 1600};
        frame.device_id = "offline-product";
        frame.viewport_id = "900x1600";
        frame.foreground_application = "jp.co.drecom.wizardry.daphne";
        frame.captured_at = std::chrono::steady_clock::now();
        frame.capture_finished_at = frame.captured_at;
        frame.connection_generation = 1;
        frame.display_rotation = 0;
        return frame;
    }
    bool execute(const wvd::contracts::Command &) override {
        throw std::runtime_error("OFFLINE_BUSINESS_INPUT_FORBIDDEN");
    }
    wvd::devices::LifecyclePort *lifecycle_port() override { return this; }
    wvd::devices::LifecycleTarget lifecycle_target() const override {
        return {"offline-product", "offline-instance", "jp.co.drecom.wizardry.daphne", "", false};
    }
    bool matches_selection(const std::filesystem::path &, int, const std::string &) const override {
        return true;
    }
    void set_vpn_required(bool value) override {
        if (value) throw std::runtime_error("OFFLINE_VPN_NOT_SUPPORTED");
    }
    J diagnostics() const override { return J::array(); }
    std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
        return wvd::devices::LifecycleObservation{lifecycle_target(), true, true, true, true,
            1, std::chrono::steady_clock::now(), true};
    }
    bool execute_lifecycle(wvd::devices::LifecycleOperation,
                           const wvd::devices::LifecycleTarget &,
                           const std::function<bool()> &) override {
        throw std::runtime_error("OFFLINE_LIFECYCLE_ACTION_FORBIDDEN");
    }
  private:
    std::vector<std::uint8_t> pixels_;
};

J call(wvd::app::Application &application, wvd::api::http::verb method,
       const std::string &path, const J &body = J::object()) {
    wvd::api::Request request{method, path, 11};
    if (method != wvd::api::http::verb::get) {
        request.body() = body.dump();
        request.prepare_payload();
    }
    const auto reply = application.handle(request);
    const auto value = J::parse(reply.body);
    if (static_cast<unsigned>(reply.status) >= 400)
        throw std::runtime_error("PRODUCT_API_" + path + ":" + value.dump());
    return value;
}
}

int main(int argc, char **argv) {
    try {
        if (argc != 3) throw std::runtime_error("PACK_AND_QUEST_PATH_REQUIRED");
        const auto pack = std::filesystem::absolute(argv[1]);
        const auto quests = std::filesystem::absolute(argv[2]);
        const auto data = std::filesystem::temp_directory_path() /
            ("wvd-native-product-" + wvd::platform::unique_id());
        auto backend = std::make_shared<OfflineConnection>(pack);
        const J document{{"schema", 1},
            {"flow", {{"id", "offline-product-check"}, {"name", "Offline product check"},
                      {"description", "Bounded native application integration"}}},
            {"entry", "Wait"},
            {"nodes", J::array({
                J{{"id", "Wait"}, {"type", "wait"}, {"name", "Wait"},
                  {"parameters", {{"duration_ms", 10}}}},
                J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
                  {"parameters", {{"outcome", "success"}}}}})},
            {"edges", J::array({J{{"id", "wait_done"}, {"from", "Wait"},
                                      {"to", "Done"}, {"outcome", "success"}, {"order", 0}}})},
            {"layout", {{"nodes", J::array({J{{"node_id", "Wait"}, {"x", 40}, {"y", 100}},
                                              J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}})},
                        {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
            {"execution", {{"time_limit_ms", 120000}}}};
        std::string revision;
        {
            wvd::app::Application writer({data, pack, {}, quests}, backend);
            auto created = call(writer, wvd::api::http::verb::post,
                                "/api/v1/workflows", document);
            revision = created.at("revision").get<std::string>();
        }
        wvd::app::Application application({data, pack, {}, quests}, backend);
        auto loaded = call(application, wvd::api::http::verb::get,
                           "/api/v1/workflows/offline-product-check");
        if (loaded.at("revision") != revision)
            throw std::runtime_error("PRODUCT_WORKFLOW_REOPEN_FAILED");
        J caller = document;
        caller["flow"]["id"] = "offline-product-caller";
        caller["flow"]["name"] = "Offline product caller";
        caller["entry"] = "Call";
        caller["nodes"] = J::array({
            J{{"id", "Call"}, {"type", "call"}, {"name", "Call"},
              {"parameters", {{"flow_id", "offline-product-check"}}}},
            J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
              {"parameters", {{"outcome", "success"}}}}});
        caller["edges"] = J::array({J{{"id", "call_done"}, {"from", "Call"},
            {"to", "Done"}, {"outcome", "success"}, {"order", 0}}});
        caller["layout"]["nodes"] = J::array({
            J{{"node_id", "Call"}, {"x", 40}, {"y", 100}},
            J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}});
        const auto created_caller = call(application, wvd::api::http::verb::post,
            "/api/v1/workflows", caller);
        wvd::api::Request deletion{wvd::api::http::verb::delete_,
            "/api/v1/workflows/offline-product-check", 11};
        deletion.body() = J{{"revision", revision}}.dump();
        deletion.prepare_payload();
        const auto rejected = application.handle(deletion);
        if (static_cast<unsigned>(rejected.status) < 400 ||
            rejected.body.find("WORKFLOW_STILL_REFERENCED") == std::string::npos)
            throw std::runtime_error("PRODUCT_REFERENCED_DELETE_NOT_REJECTED");
        deletion.target("/api/v1/workflows/offline-product-caller");
        deletion.body() = J{{"revision", created_caller.at("revision")}}.dump();
        deletion.prepare_payload();
        if (application.handle(deletion).status != wvd::api::http::status::no_content)
            throw std::runtime_error("PRODUCT_CALLER_DELETE_FAILED");
        auto accepted = call(application, wvd::api::http::verb::post,
            "/api/v1/workflows/offline-product-check/run",
            {{"revision", revision}, {"request_id", "offline-product-check-1"}});
        if (!accepted.value("accepted", false))
            throw std::runtime_error("PRODUCT_RUN_NOT_ACCEPTED");
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        J status;
        do {
            std::this_thread::sleep_for(50ms);
            status = call(application, wvd::api::http::verb::get, "/api/v1/runs/current");
            if (status.value("state", "") == "Completed" ||
                status.value("state", "") == "Failed" ||
                status.value("state", "") == "Interrupted") break;
        } while (std::chrono::steady_clock::now() < deadline);
        if (status.value("state", "") != "Completed") {
            J events = J::array();
            for (const auto &event : status.at("events").at("events"))
                events.push_back({{"type", event.value("type", "")},
                    {"node_id", event.value("node_id", J(nullptr))},
                    {"source_path", event.value("source_path", J(nullptr))}});
            throw std::runtime_error("PRODUCT_RUN_NOT_COMPLETED:" +
                status.value("reason", "") + ":" + events.dump());
        }
        if (status.value("completed_business_units", 0) != 1)
            throw std::runtime_error("PRODUCT_CHECKPOINT_MISSING");
        auto profile = call(application, wvd::api::http::verb::get, "/api/v1/profile");
        auto values = profile.at("profile");
        values["ACTIVE_BEG_MONEY"] = true;
        call(application, wvd::api::http::verb::put, "/api/v1/profile",
            {{"revision", profile.at("revision")}, {"profile", values}});
        auto task = call(application, wvd::api::http::verb::post,
            "/api/v1/runs/start",
            {{"task_id", "Scorpionesses"}, {"request_id", "offline-native-task-1"}});
        if (!task.value("accepted", false))
            throw std::runtime_error("PRODUCT_TASK_NOT_ACCEPTED");
        bool reached_native_task = false;
        const auto task_deadline = std::chrono::steady_clock::now() + 20s;
        do {
            std::this_thread::sleep_for(50ms);
            status = call(application, wvd::api::http::verb::get, "/api/v1/runs/current");
            for (const auto &event : status.at("events").at("events")) {
                if (event.value("type", "") == "step" && event.contains("node_id") &&
                    event.at("node_id").is_string() &&
                    event.at("node_id").get<std::string>().starts_with("Task_")) {
                    reached_native_task = true;
                    break;
                }
            }
            if (reached_native_task || status.value("state", "") == "Failed") break;
        } while (std::chrono::steady_clock::now() < task_deadline);
        call(application, wvd::api::http::verb::post, "/api/v1/runs/current/stop");
        if (!reached_native_task)
            throw std::runtime_error("PRODUCT_NATIVE_TASK_NOT_REACHED:" +
                status.value("reason", "") + ":" + status.value("error_code", J(nullptr)).dump());
        application.stop();
        std::cout << "Application persistence, native author run and money-handoff task entry passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
