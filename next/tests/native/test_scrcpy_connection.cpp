#include "devices/scrcpy_control.hpp"
#include <json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>

namespace {
using namespace std::chrono_literals;
using J = nlohmann::json;
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
J read(const std::filesystem::path &path) {
    std::ifstream stream(path);
    return J::parse(stream);
}
void write(const std::filesystem::path &path, const J &value) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary);
        stream << value.dump();
        require(bool(stream), "FIXTURE_WRITE_FAILED");
    }
    require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0,
        "FIXTURE_PUBLISH_FAILED");
}
template<class Predicate> void await(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < deadline, "FIXTURE_WAIT_TIMEOUT");
        std::this_thread::sleep_for(5ms);
    }
}
// Only the ADB launcher is simulated. Handshake, peek, send, close and packet
// ownership below all run the real production client over Windows TCP sockets.
int adb_fixture(int argc, char **argv) {
    wchar_t root_buffer[1024]{};
    require(GetEnvironmentVariableW(L"WVD_SCRCPY_CONNECTION_FIXTURE", root_buffer, 1024) > 0,
        "FIXTURE_ROOT_MISSING");
    const std::filesystem::path root(root_buffer);
    require(argc >= 4 && std::string(argv[2]) == "scrcpy-connection-fixture", "FIXTURE_DEVICE_INVALID");
    const std::string command(argv[3]);
    if (command == "push") return 0;
    if (command == "forward") {
        require(argc >= 5, "FIXTURE_FORWARD_INVALID");
        const std::string action(argv[4]);
        if (action == "--no-rebind") {
            require(argc == 7, "FIXTURE_FORWARD_INVALID");
            write(root / "forward.json", {{"local",argv[5]}, {"remote",argv[6]}, {"active",true}});
        } else if (action == "--list") {
            if (std::filesystem::exists(root / "forward.json")) {
                const auto mapping = read(root / "forward.json");
                if (mapping.at("active").get<bool>())
                    std::cout << argv[2] << ' ' << mapping.at("local").get<std::string>()
                        << ' ' << mapping.at("remote").get<std::string>() << '\n';
            }
        } else if (action == "--remove") {
            auto mapping = read(root / "forward.json");
            require(mapping.at("local") == argv[5], "FIXTURE_FOREIGN_FORWARD_REMOVE");
            mapping["active"] = false;
            write(root / "forward.json", mapping);
        } else throw std::runtime_error("FIXTURE_FORWARD_UNKNOWN");
        return 0;
    }
    require(command == "shell", "FIXTURE_COMMAND_UNKNOWN");
    if (argc == 5 && std::string(argv[4]).find("rm -f ") != std::string::npos) {
        std::smatch marker;
        const std::string script(argv[4]);
        require(std::regex_search(script, marker, std::regex("__WVD_RC_[0-9]+_[0-9]+__")),
            "FIXTURE_RC_MISSING");
        std::cout << '\n' << marker[0] << "0\n";
        return 0;
    }
    require(argc > 5 && std::string(argv[4]).starts_with("CLASSPATH="), "FIXTURE_SHELL_UNKNOWN");
    const auto mapping = read(root / "forward.json");
    const auto port = std::stoi(mapping.at("local").get<std::string>().substr(4));
    WSADATA data{};
    require(WSAStartup(MAKEWORD(2,2), &data) == 0, "FIXTURE_WINSOCK_FAILED");
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    require(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 &&
        listen(listener, 1) == 0, "FIXTURE_LISTEN_FAILED");
    const SOCKET peer = accept(listener, nullptr, nullptr);
    require(peer != INVALID_SOCKET, "FIXTURE_ACCEPT_FAILED");
    closesocket(listener);
    std::array<char,65> handshake{};
    handshake[1] = 'F';
    require(send(peer, handshake.data(), static_cast<int>(handshake.size()), 0) == 65,
        "FIXTURE_HANDSHAKE_FAILED");
    u_long nonblocking = 1;
    require(ioctlsocket(peer, FIONBIO, &nonblocking) == 0, "FIXTURE_NONBLOCK_FAILED");
    const auto mode = read(root / "mode.json").get<std::string>();
    unsigned received{};
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(root / "close.json")) {
            if (mode == "reset") {
                linger abortive{1,0};
                setsockopt(peer, SOL_SOCKET, SO_LINGER, reinterpret_cast<char *>(&abortive), sizeof(abortive));
            } else shutdown(peer, SD_SEND);
            closesocket(peer);
            write(root / "closed.json", true);
            // Retain a live launcher to test socket EOF independently of exit.
            if (mode == "eof") std::this_thread::sleep_for(10s);
            WSACleanup();
            return 0;
        }
        std::array<char,128> buffer{};
        const int count = recv(peer, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count > 0) {
            received += static_cast<unsigned>(count);
            write(root / "received.json", received);
        } else if (count == 0) break;
        else require(WSAGetLastError() == WSAEWOULDBLOCK, "FIXTURE_RECEIVE_FAILED");
        std::this_thread::sleep_for(5ms);
    }
    closesocket(peer);
    WSACleanup();
    return 0;
}
}

int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "-s") return adb_fixture(argc, argv);
        require(argc == 2, "NEW_ISOLATED_FIXTURE_ROOT_REQUIRED");
        const auto root = std::filesystem::absolute(argv[1]);
        require(!std::filesystem::exists(root), "FIXTURE_ROOT_ALREADY_EXISTS");
        std::filesystem::create_directories(root);
        wchar_t binary[MAX_PATH]{};
        require(GetModuleFileNameW(nullptr, binary, MAX_PATH) > 0, "FIXTURE_EXE_MISSING");
        const auto jar = root / "fixture-server.jar";
        std::ofstream(jar) << "isolated launcher fixture";
        J receipt = J::array();
        for (const auto *mode : {"eof", "reset", "live", "owned-down"}) {
            const auto directory = root / mode;
            std::filesystem::create_directories(directory);
            write(directory / "mode.json", mode);
            require(SetEnvironmentVariableW(L"WVD_SCRCPY_CONNECTION_FIXTURE", directory.c_str()) != 0,
                "FIXTURE_ENV_FAILED");
            wvd::devices::AdbCommandClient adb(binary, "scrcpy-connection-fixture");
            wvd::devices::ScrcpyControlClient client(adb, jar);
            client.connect(5s);
            require(client.connected(), "HEALTHY_CHANNEL_NOT_READY");
            const auto started = std::chrono::steady_clock::now();
            for (int n=0;n<1000;++n) require(client.connected(), "HEALTHY_CHANNEL_CHANGED");
            const auto peek_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-started).count();
            require(peek_ms < 1000, "CHANNEL_CHECK_BLOCKED");
            if (std::string(mode) == "live") {
                client.submit({.kind=wvd::contracts::ActionKind::Click,.x=450,.y=1450},900,1600);
                await([&] { return std::filesystem::exists(directory/"received.json") &&
                    read(directory/"received.json").get<unsigned>() == 64; });
                require(client.close(), "HEALTHY_CHANNEL_CLEANUP_FAILED");
            } else {
                if (std::string(mode) == "owned-down") {
                    client.submit({.kind=wvd::contracts::ActionKind::TouchDown,.x=450,.y=1450},900,1600);
                    await([&] {return std::filesystem::exists(directory/"received.json");});
                }
                write(directory/"close.json",true);
                await([&] {return std::filesystem::exists(directory/"closed.json");});
                await([&] {return !client.connected();});
                if (std::string(mode) == "owned-down") {
                    require(!client.close(), "OWNED_DOWN_CLEANUP_FALSELY_CONFIRMED");
                    require(read(directory/"received.json").get<unsigned>()==32, "UNKNOWN_INPUT_REPLAYED");
                } else {
                    require(client.close(), "DEAD_IDLE_CHANNEL_CLEANUP_FAILED");
                    // Reconnect to a new peer using the exact same production client.
                    const auto next = root / (std::string(mode)+"-reconnected");
                    std::filesystem::create_directories(next);
                    write(next/"mode.json","live");
                    SetEnvironmentVariableW(L"WVD_SCRCPY_CONNECTION_FIXTURE",next.c_str());
                    client.connect(5s);
                    require(client.connected(),"CHANNEL_REBUILD_FAILED");
                    client.submit({.kind=wvd::contracts::ActionKind::Click,.x=450,.y=1450},900,1600);
                    await([&] {return std::filesystem::exists(next/"received.json") &&
                        read(next/"received.json").get<unsigned>()==64;});
                    require(client.close(),"REBUILT_CHANNEL_CLEANUP_FAILED");
                }
            }
            receipt.push_back({{"case",mode},{"passed",true},{"1000_checks_ms",peek_ms},
                {"status",client.cleanup_status()}});
        }
        SetEnvironmentVariableW(L"WVD_SCRCPY_CONNECTION_FIXTURE",nullptr);
        write(root/"receipt.json",{{"cases",receipt},{"real_windows_socket",true},{"device_actions",false}});
        std::cout << "scrcpy real connection, rebuild and owned-input protection passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
