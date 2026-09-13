#include "runtime_fixture.hpp"
#include <iostream>

using namespace fixture;
int main(int argc, char **argv) {
    try {
        if (argc == 4) {
            const std::string mode = argv[1];
            auto signals = maafw::path_from_utf8(argv[3]);
            if (mode == "--lease-hold") {
                platform::DeviceLease lease(argv[2]);
                platform::atomic_write(signals / "ready", "ready", false);
                until([&] { return std::filesystem::exists(signals / "release"); }, 10000ms);
                return 0;
            }
            require(mode == "--lease-probe", "invalid lease test mode");
            try {
                platform::DeviceLease lease(argv[2]);
                std::cout << "acquired";
            } catch (const std::runtime_error &error) {
                require(std::string(error.what()) == "DEVICE_BUSY", error.what());
                std::cout << "busy";
            }
            return 0;
        }
        require(argc == 3, "case/result arguments required");
        MaaLoggingLevel level = MaaLoggingLevel_Off;
        MaaGlobalSetOption(MaaGlobalOption_StdoutLevel, &level, sizeof(level));
        std::ifstream file(maafw::path_from_utf8(argv[1]));
        J config;
        file >> config;
        Setup setup(config);
        std::string name = config.at("case");
        J result;
        if (name.starts_with("gate-"))
            result = gate_case(name, setup);
        else if (name.starts_with("store-"))
            result = storage_case(name, setup);
        else
            result = runtime_case(name, setup);
        result["case"] = name;
        result["pass"] = true;
        result["sdk_version"] = MaaVersion();
        std::ofstream output(maafw::path_from_utf8(argv[2]), std::ios::binary);
        output << result.dump(2);
        output.close();
        require(bool(output), "result write failed");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
