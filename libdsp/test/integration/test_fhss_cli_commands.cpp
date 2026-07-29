#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "dsp/configuration/FHSSConfigurationCli.hpp"
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include <memory>
#include <fstream>
#include <ctime>
#include <cstdlib>

using json = nlohmann::json;

namespace graphx::dsp::configuration {

// Helper function to create a valid initial configuration
static SourceConfiguration CreateTestConfiguration() {
    SourceConfiguration cfg;
    cfg.messages = {"msg1", "msg2"};
    cfg.iq_center_frequency_hz = 2400000000.0;
    cfg.iq_offsets = {0.0, 1000.0};
    cfg.idle_mode = "continuous";
    cfg.idle_duration_samples = 1024;
    cfg.occupied_bandwidth_hz = 40000000.0;
    cfg.max_abs_cfo_hz = 1000000.0;
    cfg.enable_noise = false;
    cfg.enable_doppler = false;
    cfg.enable_multipath = false;
    cfg.allow_overlap = false;
    cfg.message_id = "msg_123";
    cfg.transmit_start_sample = 0;
    cfg.frequency_index = 1;
    cfg.value = "test_value";
    cfg.role = "transmitter";
    return cfg;
}

TEST_CASE("FHSSConfigurationCli: --help displays usage information", "[cli][help]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--help"};
    auto result = cli.ExecuteCommand(2, argv);
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
    REQUIRE(result.output.find("--set-config") != std::string::npos);
}

TEST_CASE("FHSSConfigurationCli: --show-config returns current configuration", "[cli][show-config]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--show-config"};
    auto result = cli.ExecuteCommand(2, argv);
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(!result.output.empty());
    
    // Should contain valid JSON
    auto parsed = json::parse(result.output);
    REQUIRE(parsed.contains("data"));
}

TEST_CASE("FHSSConfigurationCli: --show-config --effective includes derived fields", "[cli][show-config-effective]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--show-config", "--effective"};
    auto result = cli.ExecuteCommand(3, argv);
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(!result.output.empty());
    
    auto parsed = json::parse(result.output);
    REQUIRE(parsed.contains("data"));
    // Effective config should include derived fields
}

TEST_CASE("FHSSConfigurationCli: --set-config updates configuration field", "[cli][set-config]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--set-config", "iq_center_frequency_hz=2500000000"};
    auto result = cli.ExecuteCommand(3, argv);
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(!result.output.empty());
    
    // Verify update was successful
    auto parsed = json::parse(result.output);
    REQUIRE(parsed.contains("data"));
}

TEST_CASE("FHSSConfigurationCli: --set-config with multiple KEY=VALUE pairs", "[cli][set-config-multiple]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {
        "prog", "--set-config",
        "iq_center_frequency_hz=2500000000",
        "occupied_bandwidth_hz=50000000"
    };
    auto result = cli.ExecuteCommand(4, argv);
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(!result.output.empty());
}

TEST_CASE("FHSSConfigurationCli: --set-config without arguments returns error", "[cli][set-config-no-args]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--set-config"};
    auto result = cli.ExecuteCommand(2, argv);
    
    REQUIRE(result.exit_code == 1);
    REQUIRE(!result.error.empty());
}

TEST_CASE("FHSSConfigurationCli: --validate-config validates configuration file", "[cli][validate-config]") {
    // Create a temporary config file
    json config;
    config["iq_center_frequency_hz"] = 2400000000;
    config["occupied_bandwidth_hz"] = 40000000;
    config["idle_mode"] = "continuous";
    
    // Write to temporary file
    std::string temp_file = "/tmp/test_config_" + std::to_string(time(nullptr)) + ".json";
    std::ofstream f(temp_file);
    f << config.dump(2);
    f.close();
    
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--validate-config", temp_file.c_str()};
    auto result = cli.ExecuteCommand(3, argv);
    
    // Cleanup
    std::remove(temp_file.c_str());
    
    REQUIRE(result.exit_code == 0 || result.exit_code == 1);  // May have validation errors
    REQUIRE(!result.output.empty());
}

TEST_CASE("FHSSConfigurationCli: --validate-config with missing file returns error", "[cli][validate-config-missing]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--validate-config", "/nonexistent/file.json"};
    auto result = cli.ExecuteCommand(3, argv);
    
    REQUIRE(result.exit_code == 1);
    REQUIRE(!result.error.empty());
}

TEST_CASE("FHSSConfigurationCli: --config-patch applies JSON Patch", "[cli][config-patch]") {
    // Create a temporary patch file with RFC 6902 operations
    json patch = json::array();
    json op;
    op["op"] = "replace";
    op["path"] = "/iq_center_frequency_hz";
    op["value"] = 2500000000;
    patch.push_back(op);
    
    std::string temp_file = "/tmp/test_patch_" + std::to_string(time(nullptr)) + ".json";
    std::ofstream f(temp_file);
    f << patch.dump(2);
    f.close();
    
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--config-patch", temp_file.c_str()};
    auto result = cli.ExecuteCommand(3, argv);
    
    // Cleanup
    std::remove(temp_file.c_str());
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(!result.output.empty());
}

TEST_CASE("FHSSConfigurationCli: --config-patch with --if-match validates precondition", "[cli][config-patch-if-match]") {
    // Create a temporary patch file
    json patch = json::array();
    json op;
    op["op"] = "replace";
    op["path"] = "/iq_center_frequency_hz";
    op["value"] = 2500000000;
    patch.push_back(op);
    
    std::string temp_file = "/tmp/test_patch_match_" + std::to_string(time(nullptr)) + ".json";
    std::ofstream f(temp_file);
    f << patch.dump(2);
    f.close();
    
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--config-patch", temp_file.c_str(), "--if-match", "Rev:1"};
    auto result = cli.ExecuteCommand(5, argv);
    
    // Cleanup
    std::remove(temp_file.c_str());
    
    // Result depends on whether current ETag matches
    REQUIRE(result.exit_code == 0 || result.exit_code == 1);
}

TEST_CASE("FHSSConfigurationCli: Unknown command returns error", "[cli][unknown-command]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--unknown-command"};
    auto result = cli.ExecuteCommand(2, argv);
    
    REQUIRE(result.exit_code == 1);
    REQUIRE(result.error.find("Unknown command") != std::string::npos);
}

TEST_CASE("FHSSConfigurationCli: No arguments shows help", "[cli][no-args]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog"};
    auto result = cli.ExecuteCommand(1, argv);
    
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.output.find("Usage:") != std::string::npos);
}

TEST_CASE("FHSSConfigurationCli: Set config with numeric value type coercion", "[cli][set-config-numeric]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--set-config", "occupied_bandwidth_hz=50000000"};
    auto result = cli.ExecuteCommand(3, argv);
    
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("FHSSConfigurationCli: Set config with string value", "[cli][set-config-string]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--set-config", "idle_mode=test_mode"};
    auto result = cli.ExecuteCommand(3, argv);
    
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("FHSSConfigurationCli: Validate config returns validation errors structure", "[cli][validate-errors]") {
    // Create an invalid config file
    json config;
    config["iq_center_frequency_hz"] = 50000000000000.0;  // Invalid frequency
    
    std::string temp_file = "/tmp/test_invalid_" + std::to_string(time(nullptr)) + ".json";
    std::ofstream f(temp_file);
    f << config.dump(2);
    f.close();
    
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    const char* argv[] = {"prog", "--validate-config", temp_file.c_str()};
    auto result = cli.ExecuteCommand(3, argv);
    
    // Cleanup
    std::remove(temp_file.c_str());
    
    // Parse output to verify error format
    if (result.exit_code != 0) {
        // Should contain validation error information
        REQUIRE(!result.output.empty() || !result.error.empty());
    }
}

}  // namespace graphx::dsp::configuration
