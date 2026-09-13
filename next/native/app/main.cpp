#include "api/http_server.hpp"
#include "contracts/version.hpp"
#include <charconv>
#include <csignal>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    try {
        unsigned short port = 17652;
        std::filesystem::path root;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--version") {
                std::cout << "automationd " << wvd::contracts::service_version
                          << " api=1 stage=M1\n";
                return 0;
            }
            if ((arg == "--port" || arg == "--web-root") && i + 1 < argc) {
                std::string value = argv[++i];
                if (arg == "--web-root")
                    root = std::filesystem::path(std::u8string(value.begin(), value.end()));
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
                    "usage: automationd --web-root PATH [--port PORT] | --version");
        }
        if (root.empty() || !std::filesystem::is_regular_file(root / "index.html"))
            throw std::runtime_error("built web root with index.html required");
        boost::asio::io_context io{1};
        wvd::api::HttpServer server(io, port, std::move(root));
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
#ifdef SIGBREAK
        signals.add(SIGBREAK);
#endif
        signals.async_wait([&](const boost::system::error_code &ec, int) {
            if (!ec)
                server.stop();
        });
        server.start();
        std::cout << "READY http://127.0.0.1:" << server.port() << "\n" << std::flush;
        io.run();
        std::cout << "STOPPED\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "STARTUP_ERROR: " << error.what() << "\n";
        return 1;
    }
}
