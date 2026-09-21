#include "api/http_server.hpp"
#include "app/application.hpp"
#include "contracts/version.hpp"
#include <charconv>
#include <csignal>
#include <iostream>
#include <string>
#include <windows.h>
#include <shellapi.h>

namespace {
boost::asio::io_context *shutdown_context{};
BOOL WINAPI console_control(DWORD type) {
    if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
         type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) && shutdown_context) {
        // 控制台回调只发停止信号；设备与 Run 的有序清理由主线程执行。
        shutdown_context->stop();
        return TRUE;
    }
    return FALSE;
}
} // namespace

int main(int argc, char **argv) {
    try {
        unsigned short port = 17652;
        std::filesystem::path root, data_root, pack_root, legacy_config, quests;
        bool open_browser = true;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
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
                std::string value = argv[++i];
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
        shutdown_context = &io;
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
        signals.async_wait([&](const boost::system::error_code &ec, int) {
            if (!ec)
                application.stop();
            if (!ec)
                server.stop();
        });
        server.start();
        const auto url = "http://127.0.0.1:" + std::to_string(server.port());
        std::cout << "READY " << url << "\n" << std::flush;
        if (open_browser)
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        io.run();
        application.stop();
        server.stop();
        shutdown_context = nullptr;
        SetConsoleCtrlHandler(console_control, FALSE);
        std::cout << "STOPPED\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "STARTUP_ERROR: " << error.what() << "\n";
        return 1;
    }
}
