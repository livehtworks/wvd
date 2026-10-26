#include "api/http_server.hpp"
#include "app/application.hpp"
#include "app/service_instance.hpp"
#include "app/publication_maintenance.hpp"
#include "contracts/version.hpp"
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/process.hpp"
#include <charconv>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <set>
#include <windows.h>
#include <shellapi.h>

namespace {
std::atomic<bool> shutdown_requested{false};
BOOL WINAPI console_control(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        shutdown_requested = true; // no SDK, no pointer to a stack io_context
        return TRUE;
    }
    return FALSE;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        bool maintenance = false;
        for (int i = 1; i < argc; ++i) if (std::wstring(argv[i]) == L"--maintenance-publication") maintenance = true;
        if (maintenance) {
            std::string command, request, transaction, repeat_root;
            std::optional<wvd::app::PublicationMaintenance::RepeatEvidence> repeat;
            std::uint64_t cycle{};
            std::filesystem::path maintenance_root;
            std::set<std::wstring> seen;
            for (int i = 1; i < argc; ++i) {
                const std::wstring option(argv[i]);
                if ((option != L"--maintenance-publication" && option != L"--data-root" &&
                    option != L"--request-id" && option != L"--transaction-id" &&
                    option != L"--root-request-id" && option != L"--cycle") ||
                    !seen.insert(option).second || i + 1 >= argc)
                    throw std::runtime_error("PUBLICATION_MAINTENANCE_ARGUMENT_INVALID");
                const std::filesystem::path value(argv[++i]);
                if (option == L"--data-root") maintenance_root = value;
                else if (option == L"--maintenance-publication") command = wvd::platform::utf8(value);
                else if (option == L"--request-id") request = wvd::platform::utf8(value);
                else if (option == L"--root-request-id") repeat_root = wvd::platform::utf8(value);
                else if (option == L"--cycle") {
                    const auto text = wvd::platform::utf8(value);
                    const auto [end,error] = std::from_chars(text.data(),text.data()+text.size(),cycle);
                    if (error != std::errc{} || end != text.data()+text.size())
                        throw std::runtime_error("PUBLICATION_MAINTENANCE_ARGUMENT_INVALID");
                }
                else transaction = wvd::platform::utf8(value);
            }
            const bool by_request = command == "plan" || command == "convert";
            if (maintenance_root.empty() || (by_request ? request.empty() || !transaction.empty() : transaction.empty() || !request.empty()))
                throw std::runtime_error("PUBLICATION_MAINTENANCE_ARGUMENT_INVALID");
            if (seen.contains(L"--root-request-id") != seen.contains(L"--cycle") ||
                (seen.contains(L"--cycle") && (!by_request || repeat_root.empty())))
                throw std::runtime_error("PUBLICATION_MAINTENANCE_ARGUMENT_INVALID");
            if (seen.contains(L"--cycle")) repeat = wvd::app::PublicationMaintenance::RepeatEvidence{repeat_root,cycle};
            std::cout << wvd::app::PublicationMaintenance::execute(maintenance_root, command,
                by_request ? request : transaction,{},repeat).dump() << '\n';
            return 0;
        }
        unsigned short port = 17652;
        std::filesystem::path root, data_root, pack_root, legacy_config, quests, stdout_log, stderr_log;
        bool open_browser = true;
        bool background = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = wvd::platform::utf8(std::filesystem::path(argv[i]));
            if (arg == "--version") {
                std::cout << "automationd " << wvd::contracts::service_version
                          << " api=1 stage=WINDOWS_FUNCTIONAL\n";
                return 0;
            }
            if (arg == "--no-browser") {
                open_browser = false;
                continue;
            }
            if (arg == "--launch") { background = true; continue; }
            if ((arg == "--port" || arg == "--web-root" || arg == "--data-root" ||
                 arg == "--pack-root" || arg == "--legacy-config" || arg == "--quests" ||
                 arg == "--stdout-log" || arg == "--stderr-log") &&
                i + 1 < argc) {
                std::string value = wvd::platform::utf8(std::filesystem::path(argv[++i]));
                if (arg == "--web-root")
                    root = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else if (arg == "--data-root")
                    data_root = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else if (arg == "--pack-root")
                    pack_root = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else if (arg == "--legacy-config")
                    legacy_config = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else if (arg == "--quests")
                    quests = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else if (arg == "--stdout-log")
                    stdout_log = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else if (arg == "--stderr-log")
                    stderr_log = std::filesystem::path(std::u8string(value.begin(), value.end()));
                else {
                    unsigned int number = 0;
                    auto [end, ec] =
                        std::from_chars(value.data(), value.data() + value.size(), number);
                    if (ec != std::errc{} || end != value.data() + value.size() || number > 65535)
                        throw std::runtime_error("invalid port");
                    port = static_cast<unsigned short>(number);
                }
            } else
                throw std::runtime_error(
                    "usage: automationd --web-root PATH --data-root PATH --pack-root PATH "
                    "--quests FILE [--legacy-config FILE] [--port PORT] [--no-browser] "
                    "[--launch --stdout-log FILE --stderr-log FILE] | --version");
        }
        if (root.empty() || !std::filesystem::is_regular_file(root / "index.html"))
            throw std::runtime_error("built web root with index.html required");
        if (data_root.empty() || pack_root.empty() || quests.empty())
            throw std::runtime_error("application data, pack and quest paths are required");
        if (background) {
            if (stdout_log.empty() || stderr_log.empty())
                throw std::runtime_error("background stdout/stderr log paths are required");
            std::vector<std::wstring> forwarded;
            for (int i = 1; i < argc; ++i) {
                const std::wstring arg(argv[i]);
                if (arg == L"--launch") continue;
                if (arg == L"--stdout-log" || arg == L"--stderr-log") { ++i; continue; }
                forwarded.push_back(arg);
            }
            wchar_t executable[32768]{};
            const auto length = GetModuleFileNameW(nullptr, executable, 32768);
            if (!length || length >= 32768) throw std::runtime_error("SERVICE_EXECUTABLE_PATH_FAILED");
            const auto pid = wvd::platform::launch_background(executable, forwarded,
                std::filesystem::absolute(stdout_log), std::filesystem::absolute(stderr_log));
            std::cout << nlohmann::json{{"pid", pid}}.dump() << '\n';
            return 0;
        }
        if (!stdout_log.empty() || !stderr_log.empty())
            throw std::runtime_error("log paths are only valid with --launch");
        wvd::app::ServiceInstance instance(data_root);
        wvd::app::PublicationMaintenance::require_no_pending(std::filesystem::absolute(data_root));
        wvd::app::Application application({std::filesystem::absolute(data_root),
                                           std::filesystem::absolute(pack_root),
                                           legacy_config.empty() ? legacy_config
                                                                 : std::filesystem::absolute(legacy_config),
                                           std::filesystem::absolute(quests), instance.id()});
        boost::asio::io_context io{1};
        shutdown_requested = false;
        SetConsoleCtrlHandler(console_control, TRUE);
        wvd::api::HttpServer server(
            io, port, std::move(root),
            [&](const wvd::api::Request &request) {
                using namespace wvd::api;
                using Json = nlohmann::json;
                const auto raw = std::string(request.target());
                const auto path = raw.substr(0, raw.find('?'));
                if (path == "/api/v1/service" && request.method() == http::verb::get)
                    return std::optional<DynamicReply>(DynamicReply{http::status::ok, instance.identity().dump()});
                if (path == "/api/v1/service/shutdown" && request.method() == http::verb::post) {
                    const auto body = Json::parse(request.body(), nullptr, false);
                    if (!body.is_object() || !body.contains("instance_id") || body["instance_id"] != instance.id())
                        return std::optional<DynamicReply>(DynamicReply{http::status::conflict,
                            R"({"error_code":"SERVICE_INSTANCE_MISMATCH"})"});
                    instance.mark_stopping();
                    application.request_shutdown();
                    return std::optional<DynamicReply>(DynamicReply{http::status::accepted,
                        Json{{"state", "stopping"}, {"instance_id", instance.id()}}.dump(),
                        "application/json; charset=utf-8", [] { shutdown_requested = true; }});
                }
                return std::optional<wvd::api::DynamicReply>(application.handle(request));
            });
        instance.publish(server.port());
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
#ifdef SIGBREAK
        signals.add(SIGBREAK);
#endif
        boost::asio::steady_timer shutdown_timer(io);
        bool shutdown_started = false;
        auto begin_shutdown = [&] {
            if (shutdown_started) return;
            shutdown_started = true;
            instance.mark_stopping();
            // I/O线程只关闭准入并发出取消；设备与工作线程在 io.run 返回后回收。
            application.request_shutdown();
            server.stop();
            signals.cancel();
            shutdown_timer.cancel();
        };
        std::function<void()> poll_shutdown;
        poll_shutdown = [&] {
            shutdown_timer.expires_after(std::chrono::milliseconds{100});
            shutdown_timer.async_wait([&](const boost::system::error_code &ec) {
                if (ec) return;
                if (shutdown_requested) begin_shutdown(); else poll_shutdown();
            });
        };
        poll_shutdown();
        signals.async_wait([&](const boost::system::error_code &ec, int) {
            if (!ec) begin_shutdown();
        });
        server.start();
        const auto url = "http://127.0.0.1:" + std::to_string(server.port());
        std::cout << "READY " << url << "\n" << std::flush;
        if (open_browser)
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        io.run();
        application.request_shutdown();
        server.stop();
        server.join_workers();
        // 所有 worker 已不再产生新回调，再销毁/执行剩余的关闭回调。
        io.restart();
        io.poll();
        application.stop();
        SetConsoleCtrlHandler(console_control, FALSE);
        std::cout << "STOPPED\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "STARTUP_ERROR: " << error.what() << "\n";
        return 1;
    }
}
