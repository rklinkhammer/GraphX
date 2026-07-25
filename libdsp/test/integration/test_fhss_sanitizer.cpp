#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "dsp/configuration/FHSSConfigurationHttpServer.hpp"
#include "dsp/configuration/FHSSConfigurationCli.hpp"
#include <memory>
#include <vector>

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

TEST_CASE("FHSSConfigurationHttpServer: No buffer overflow on large JSON response", "[sanitizer][buffer-overflow]") {
    auto cfg = CreateTestConfiguration();
    
    // Create large config with many messages
    for (int i = 0; i < 1000; ++i) {
        cfg.messages.push_back("large_msg_" + std::to_string(i));
    }
    
    auto state_machine = std::make_shared<ConfigurationStateMachine>(cfg);
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    // This should not crash or overflow buffers
    bool result = handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(response_body.size() > 0);
}

TEST_CASE("FHSSConfigurationHttpServer: No use-after-free on handler reuse", "[sanitizer][use-after-free]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Call handler multiple times to detect use-after-free
    for (int i = 0; i < 100; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        
        REQUIRE(status == 200);
    }
}

TEST_CASE("FHSSConfigurationHttpServer: No memory leak on error responses", "[sanitizer][memory-leak]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Generate many error responses
    for (int i = 0; i < 50; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        // Trigger 404 errors
        handler("GET", "/api/v2/invalid/endpoint/" + std::to_string(i), {}, "", status, response_headers, response_body);
        
        REQUIRE(status == 404);
    }
}

TEST_CASE("FHSSConfigurationHttpServer: No memory leak on malformed JSON", "[sanitizer][malformed-json]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Send many malformed JSON payloads
    std::vector<std::string> malformed = {
        "{invalid",
        "{ \"unclosed\": ",
        "{ \"key\": }",
        "{\"array\": [1, 2, 3}",
        "{ \"nested\": { \"value\": \"unclosed\" }",
    };
    
    for (const auto& payload : malformed) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("POST", "/api/v2/fhss/config/staged", {}, payload, status, response_headers, response_body);
        
        REQUIRE(status == 400);
    }
}

TEST_CASE("FHSSConfigurationHttpServer: No double-free or invalid free", "[sanitizer][double-free]") {
    {
        auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
        FHSSConfigurationHttpServer server(state_machine);
        
        auto handler = server.GetRequestHandler();
        
        // Use handler
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    }
    // scope ends, objects destroyed - should not double-free
}

TEST_CASE("FHSSConfigurationCli: No buffer overflow on large argument list", "[sanitizer][cli-buffer-overflow]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    // Create many KEY=VALUE arguments
    std::vector<const char*> argv_vec;
    argv_vec.push_back("prog");
    argv_vec.push_back("--set-config");
    
    std::vector<std::string> args;
    for (int i = 0; i < 100; ++i) {
        args.push_back("field_" + std::to_string(i) + "=value_" + std::to_string(i));
        argv_vec.push_back(args.back().c_str());
    }
    
    auto result = cli.ExecuteCommand(argv_vec.size(), argv_vec.data());
    
    // Should handle large argument list gracefully
    REQUIRE(result.exit_code == 0 || result.exit_code == 1);
}

TEST_CASE("FHSSConfigurationCli: No memory leak on file reading errors", "[sanitizer][cli-file-leak]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationCli cli(state_machine);
    
    // Try to read many non-existent files
    for (int i = 0; i < 50; ++i) {
        const char* argv[] = {"prog", "--validate-config", "/nonexistent/file_12345.json"};
        auto result = cli.ExecuteCommand(3, argv);
        
        REQUIRE(result.exit_code == 1);
    }
}

TEST_CASE("FHSSConfigurationHttpServer: No undefined behavior on empty paths", "[sanitizer][undefined-behavior]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Test with empty and edge case paths
    std::vector<std::string_view> paths = {
        "",
        "/",
        "//",
        "///api",
        "/api///v2",
    };
    
    for (const auto& path : paths) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        // Should not crash
        handler("GET", path, {}, "", status, response_headers, response_body);
    }
}

TEST_CASE("FHSSConfigurationHttpServer: No invalid pointer dereference on edge cases", "[sanitizer][pointer-safety]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Test with empty headers
    {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        REQUIRE(status == 200);
    }
    
    // Test with large header list
    {
        std::vector<std::pair<std::string, std::string>> large_headers;
        for (int i = 0; i < 1000; ++i) {
            large_headers.emplace_back("Header-" + std::to_string(i), "Value-" + std::to_string(i));
        }
        
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("POST", "/api/v2/fhss/config/commit", large_headers, "{}", status, response_headers, response_body);
    }
}

TEST_CASE("FHSSConfigurationHttpServer: No signed/unsigned integer overflow", "[sanitizer][integer-overflow]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    // State machine operations should not overflow
    auto handler = server.GetRequestHandler();
    
    for (int i = 0; i < 10; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        
        auto response = json::parse(response_body);
        auto revision = response["revision"].get<int>();
        REQUIRE(revision >= 0);  // Should never be negative
    }
}

}  // namespace graphx::dsp::configuration
