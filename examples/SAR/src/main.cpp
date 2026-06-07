#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const char* defaultConfig = "examples/SAR/config/sar_stripmap_pr1.json";
    const char* configPath = (argc > 1) ? argv[1] : defaultConfig;

    std::cout << "GraphX SAR example scaffold" << '\n';
    std::cout << "Topology config: " << configPath << '\n';

    if (!std::filesystem::exists(configPath)) {
        std::cerr << "Config file not found: " << configPath << '\n';
        return 1;
    }

    std::cout << "Scaffold ready. PR1 node pipeline wiring is pending." << '\n';
    return 0;
}
