#include "api/http_server.hpp"
#include "app/application.hpp"
#include "contracts/version.hpp"
#include "platform/windows/path_utf8.hpp"
#include <charconv>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
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
        unsigned short port = 17652;
        std::filesystem::path root, data_root, pack_root, legacy_config, quests;
        bool open_browser = true;
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
            if ((arg == "--port" || arg == "--web-root" || arg == "--data-root" ||
                 arg == "--pack-root" || arg == "--legacy-config" || arg == "--quests") &&
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
                    "--quests FILE [--legacy-config FILE] [--port PORT] [--no-browser] | --version");
        }
        if (root.empty() || !std::filesystem::is_regular_file(root / "index.html"))
            throw std::runtime_error("built web root with index.html required");
        if (data_root.empty() || pack_root.empty() || quests.empty())
            throw std::runtime_error("application data, pack and quest paths are required");
        wvd::app::Application application({std::filesystem::absolute(data_root),
                                           std::filesystem::absolute(pack_root),
                                           legacy_config.empty() ? legacy_config
                                                                 : std::filesystem::absolute(legacy_config),
                                           std::filesystem::absolute(quests)});
        boost::asio::io_context io{1};
        shutdown_requested = false;
        SetConsoleCtrlHandler(console_control, TRUE);
        wvd::api::HttpServer server(
            io, port, std::move(root),
            [&](const wvd::api::Request &request) {
                return std::optional<wvd::api::DynamicReply>(application.handle(request));
            });
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
#ifdef SIGBREAK
        signals.add(SIGBREAK);
#endif
        boost::asio::steady_timer shutdown_timer(io);
        bool shutdown_started = false;
        auto begin_shutdown = [&] {
            if (shutdown_started) return;
            shutdown_started = true;
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
