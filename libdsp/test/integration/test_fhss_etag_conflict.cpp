#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "dsp/configuration/FHSSConfigurationHttpServer.hpp"
#include <memory>
#include <thread>

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

TEST_CASE("FHSSConfigurationHttpServer: 409 Conflict returned when If-Match ETag is stale", "[etag-conflict][409]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Attempt to commit with stale If-Match
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request;
    request["staged_id"] = "test-123";
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", "Rev:999");  // Obviously stale
    
    bool result = handler("POST", "/api/v2/fhss/config/commit", headers, request.dump(), status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 409);
}

TEST_CASE("FHSSConfigurationHttpServer: 409 Conflict response includes current and expected ETags", "[etag-conflict][etag-details]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request;
    request["staged_id"] = "test-123";
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", "Rev:999");
    
    handler("POST", "/api/v2/fhss/config/commit", headers, request.dump(), status, response_headers, response_body);
    
    auto response = json::parse(response_body);
    
    REQUIRE(response.contains("current_etag"));
    REQUIRE(response.contains("expected_etag"));
    REQUIRE(response["current_etag"] != response["expected_etag"]);
}

TEST_CASE("FHSSConfigurationHttpServer: 200 OK returned when If-Match ETag matches current", "[etag-conflict][200-ok]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // First get the current ETag
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    auto current_response = json::parse(response_body);
    auto current_etag = current_response["etag"].get<std::string>();
    
    // Now try to commit with matching If-Match
    status = 0;
    response_headers.clear();
    response_body.clear();
    
    json request;
    request["staged_id"] = "test-123";
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", current_etag);
    
    handler("POST", "/api/v2/fhss/config/commit", headers, request.dump(), status, response_headers, response_body);
    
    REQUIRE(status == 200);
}

TEST_CASE("FHSSConfigurationHttpServer: ETag format is RFC 7232 compliant", "[etag-conflict][format]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    auto response = json::parse(response_body);
    auto etag = response["etag"].get<std::string>();
    
    // ETag should match pattern "Rev:N" where N is a number
    REQUIRE(etag.size() > 4);
    REQUIRE(etag.substr(0, 4) == "Rev:");
    
    // Check all characters after "Rev:" are digits
    for (size_t i = 4; i < etag.size(); ++i) {
        REQUIRE(std::isdigit(etag[i]));
    }
}

TEST_CASE("FHSSConfigurationHttpServer: Conflict response is RFC 9457 compliant", "[etag-conflict][rfc9457]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request;
    request["staged_id"] = "test-123";
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", "Rev:999");
    
    handler("POST", "/api/v2/fhss/config/commit", headers, request.dump(), status, response_headers, response_body);
    
    auto response = json::parse(response_body);
    
    // RFC 9457 Problem Details required fields
    REQUIRE(response.contains("type"));
    REQUIRE(response.contains("status"));
    REQUIRE(response.contains("title"));
    REQUIRE(response.contains("detail"));
    REQUIRE(response.contains("instance"));
    
    // Verify values
    REQUIRE(response["type"] == "about:blank");
    REQUIRE(response["status"] == 409);
    REQUIRE(response["title"] == "Conflict");
}

TEST_CASE("FHSSConfigurationHttpServer: Concurrent edit conflict detection (simulated)", "[etag-conflict][concurrent]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Simulate two concurrent edit attempts
    // First request gets current ETag
    int status1 = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status1, response_headers, response_body);
    auto first_etag = json::parse(response_body)["etag"].get<std::string>();
    
    // Second request also gets same ETag
    status1 = 0;
    response_headers.clear();
    handler("GET", "/api/v2/fhss/config", {}, "", status1, response_headers, response_body);
    auto second_etag = json::parse(response_body)["etag"].get<std::string>();
    
    REQUIRE(first_etag == second_etag);
}

TEST_CASE("FHSSConfigurationHttpServer: If-Match header takes precedence over body", "[etag-conflict][header-priority]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request;
    request["staged_id"] = "test-123";
    request["if_match"] = "Rev:111";  // Different from header
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", "Rev:999");  // This should take precedence
    
    handler("POST", "/api/v2/fhss/config/commit", headers, request.dump(), status, response_headers, response_body);
    
    // Should conflict with Rev:999, not Rev:111
    REQUIRE(status == 409);
    auto response = json::parse(response_body);
    REQUIRE(response["expected_etag"] == "Rev:999");
}

TEST_CASE("FHSSConfigurationHttpServer: Stale writes prevented with If-Match precondition", "[etag-conflict][prevent-stale]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    // Get current revision
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    auto current_revision = json::parse(response_body)["revision"].get<int>();
    
    // Attempt commit with old If-Match should fail
    json request;
    request["staged_id"] = "test-123";
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", "Rev:" + std::to_string(current_revision - 1));
    
    handler("POST", "/api/v2/fhss/config/commit", headers, request.dump(), status, response_headers, response_body);
    
    REQUIRE(status == 409);  // Should be conflict
}

}  // namespace graphx::dsp::configuration
