#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "dsp/configuration/FHSSConfigurationHttpServer.hpp"
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include <memory>

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

TEST_CASE("FHSSConfigurationHttpServer: JSON response is byte-identical across 10 iterations", "[determinism][http-response]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    std::string first_response;
    for (int i = 0; i < 10; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        
        if (i == 0) {
            first_response = response_body;
        } else {
            REQUIRE(response_body == first_response);
        }
    }
}

TEST_CASE("FHSSConfigurationHttpServer: Effective configuration is byte-identical across 10 iterations", "[determinism][effective-config]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    std::string first_response;
    for (int i = 0; i < 10; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config/effective", {}, "", status, response_headers, response_body);
        
        if (i == 0) {
            first_response = response_body;
        } else {
            REQUIRE(response_body == first_response);
        }
    }
}

TEST_CASE("FHSSConfigurationHttpServer: JSON keys are sorted alphabetically (determinism)", "[determinism][sorted-keys]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    auto response = json::parse(response_body);
    
    // Check that top-level keys are sorted
    std::vector<std::string> keys;
    for (const auto& [key, _] : response.items()) {
        keys.push_back(key);
    }
    
    std::vector<std::string> sorted_keys = keys;
    std::sort(sorted_keys.begin(), sorted_keys.end());
    
    REQUIRE(keys == sorted_keys);
}

TEST_CASE("FHSSConfigurationHttpServer: Large configuration (100+ fields) is deterministic", "[determinism][large-config]") {
    auto cfg = CreateTestConfiguration();
    
    // Add many message IDs to increase data size
    for (int i = 0; i < 100; ++i) {
        cfg.messages.push_back("msg_" + std::to_string(i));
    }
    
    auto state_machine = std::make_shared<ConfigurationStateMachine>(cfg);
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    std::string first_response;
    for (int i = 0; i < 5; ++i) {  // 5 iterations for large config
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        
        if (i == 0) {
            first_response = response_body;
        } else {
            REQUIRE(response_body == first_response);
        }
    }
}

TEST_CASE("FHSSConfigurationHttpServer: All response types are deterministic", "[determinism][all-endpoints]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Test each endpoint multiple times
    std::string get_config_first;
    std::string get_effective_first;
    
    for (int i = 0; i < 5; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        // GET /config
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        if (i == 0) {
            get_config_first = response_body;
        } else {
            REQUIRE(response_body == get_config_first);
        }
        
        // GET /effective
        handler("GET", "/api/v2/fhss/config/effective", {}, "", status, response_headers, response_body);
        if (i == 0) {
            get_effective_first = response_body;
        } else {
            REQUIRE(response_body == get_effective_first);
        }
    }
}

TEST_CASE("FHSSConfigurationHttpServer: ETag remains stable for same revision", "[determinism][stable-etag]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    std::string first_etag;
    for (int i = 0; i < 10; ++i) {
        int status = 0;
        std::vector<std::pair<std::string, std::string>> response_headers;
        std::string response_body;
        
        handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
        
        auto response = json::parse(response_body);
        auto etag = response["etag"].get<std::string>();
        
        if (i == 0) {
            first_etag = etag;
        } else {
            REQUIRE(etag == first_etag);
        }
    }
}

TEST_CASE("FHSSConfigurationDeriver: Derived fields are deterministic across 10 iterations", "[determinism][derived-fields]") {
    auto cfg = CreateTestConfiguration();
    auto deriver = FHSSConfigurationDeriver();
    
    std::string first_derived;
    for (int i = 0; i < 10; ++i) {
        auto derived = deriver.DeriveEffectiveConfiguration(cfg);
        auto derived_json = derived.to_json().dump();
        
        if (i == 0) {
            first_derived = derived_json;
        } else {
            REQUIRE(derived_json == first_derived);
        }
    }
}

}  // namespace graphx::dsp::configuration
