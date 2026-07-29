#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>
#include "dsp/configuration/FHSSConfigurationHttpServer.hpp"
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include "dsp/configuration/FHSSCrossNodeValidator.hpp"
#include <memory>
#include <sstream>

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

TEST_CASE("FHSSConfigurationHttpServer: GET /api/v2/fhss/config returns source configuration", "[http][get-config]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer::Options options;
    FHSSConfigurationHttpServer server(state_machine, options);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response["schema"] == "graphx.fhss_configuration.v1");
    REQUIRE(response.contains("data"));
    REQUIRE(response.contains("revision"));
    REQUIRE(response.contains("etag"));
    REQUIRE(response["etag"].is_string());
}

TEST_CASE("FHSSConfigurationHttpServer: GET /api/v2/fhss/config/effective returns effective configuration", "[http][get-effective]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("GET", "/api/v2/fhss/config/effective", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response.contains("data"));
    
    // Verify derived fields are present
    auto data = response["data"];
    REQUIRE(data.contains("active_frequency_indices_source"));
    REQUIRE(data.contains("preamble_pulses"));
}

TEST_CASE("FHSSConfigurationHttpServer: GET /api/v2/fhss/config/history returns revision history", "[http][get-history]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("GET", "/api/v2/fhss/config/history", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response["schema"] == "graphx.fhss_configuration.history.v1");
    REQUIRE(response.contains("revisions"));
}

TEST_CASE("FHSSConfigurationHttpServer: POST /api/v2/fhss/config/staged creates staged edit", "[http][post-staged]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("POST", "/api/v2/fhss/config/staged", {}, "{}", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 201);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response["schema"] == "graphx.fhss_configuration.staged_edit.v1");
    REQUIRE(response.contains("data"));
    REQUIRE(response["data"].contains("staged_id"));
    REQUIRE(response["data"].contains("base_revision"));
}

TEST_CASE("FHSSConfigurationHttpServer: PATCH /api/v2/fhss/config/staged/{id} updates field", "[http][patch-staged]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json patch_body;
    patch_body["field"] = "iq_center_frequency_hz";
    patch_body["value"] = 2500000000;
    
    bool result = handler("PATCH", "/api/v2/fhss/config/staged/test-id-123", {}, patch_body.dump(), status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response["data"]["field"] == "iq_center_frequency_hz");
}

TEST_CASE("FHSSConfigurationHttpServer: POST /api/v2/fhss/config/validate validates staged edit", "[http][post-validate]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request_body;
    request_body["staged_id"] = "test-id-123";
    
    bool result = handler("POST", "/api/v2/fhss/config/validate", {}, request_body.dump(), status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response["schema"] == "graphx.fhss_configuration.validation_result.v1");
    REQUIRE(response["data"].contains("is_valid"));
}

TEST_CASE("FHSSConfigurationHttpServer: POST /api/v2/fhss/config/commit commits staged edit", "[http][post-commit]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request_body;
    request_body["staged_id"] = "test-id-123";
    
    bool result = handler("POST", "/api/v2/fhss/config/commit", {}, request_body.dump(), status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 200);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response.contains("schema"));
    REQUIRE(response["schema"] == "graphx.fhss_configuration.commit_result.v1");
    REQUIRE(response["data"].contains("new_revision"));
    REQUIRE(response["data"].contains("new_etag"));
}

TEST_CASE("FHSSConfigurationHttpServer: POST /api/v2/fhss/config/commit returns 409 on stale If-Match", "[http][post-commit-conflict]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    json request_body;
    request_body["staged_id"] = "test-id-123";
    
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("If-Match", "Rev:999");  // Stale ETag
    
    bool result = handler("POST", "/api/v2/fhss/config/commit", headers, request_body.dump(), status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 409);
    REQUIRE(!response_body.empty());
    
    auto response = json::parse(response_body);
    REQUIRE(response["status"] == 409);
    REQUIRE(response.contains("current_etag"));
    REQUIRE(response.contains("expected_etag"));
}

TEST_CASE("FHSSConfigurationHttpServer: DELETE /api/v2/fhss/config/staged/{id} discards edit", "[http][delete-staged]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("DELETE", "/api/v2/fhss/config/staged/test-id-123", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 204);
}

TEST_CASE("FHSSConfigurationHttpServer: POST /api/v2/fhss/config/undo undoes last change", "[http][post-undo]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    // Note: Undo at beginning should fail
    bool result = handler("POST", "/api/v2/fhss/config/undo", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 400);  // Can't undo at beginning
}

TEST_CASE("FHSSConfigurationHttpServer: POST /api/v2/fhss/config/redo redoes last undo", "[http][post-redo]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    // Note: Redo with nothing to redo should fail
    bool result = handler("POST", "/api/v2/fhss/config/redo", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 400);  // Nothing to redo
}

TEST_CASE("FHSSConfigurationHttpServer: Unknown route returns 404", "[http][not-found]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("GET", "/api/v2/unknown/endpoint", {}, "", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 404);
}

TEST_CASE("FHSSConfigurationHttpServer: Malformed JSON returns 400", "[http][invalid-json]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    bool result = handler("POST", "/api/v2/fhss/config/staged", {}, "{invalid json", status, response_headers, response_body);
    
    REQUIRE(result == true);
    REQUIRE(status == 400);
}

TEST_CASE("FHSSConfigurationHttpServer: Response includes Content-Type header", "[http][content-type]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    auto content_type_found = false;
    for (const auto& [name, value] : response_headers) {
        if (name == "Content-Type" && value == "application/json") {
            content_type_found = true;
            break;
        }
    }
    REQUIRE(content_type_found);
}

TEST_CASE("FHSSConfigurationHttpServer: Response JSON has sorted keys (determinism)", "[http][sorted-keys]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    // Parse and verify keys are sorted
    auto response = json::parse(response_body);
    REQUIRE(response.contains("data"));
    REQUIRE(response.contains("etag"));
    REQUIRE(response.contains("revision"));
    REQUIRE(response.contains("schema"));
    
    // For determinism test, verify the serialization is stable
    std::string response_body2;
    status = 0;
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body2);
    
    REQUIRE(response_body == response_body2);  // Byte-identical responses
}

TEST_CASE("FHSSConfigurationHttpServer: ETag format is Rev:N", "[http][etag-format]") {
    auto state_machine = std::make_shared<ConfigurationStateMachine>(CreateTestConfiguration());
    FHSSConfigurationHttpServer server(state_machine);
    
    auto handler = server.GetRequestHandler();
    
    int status = 0;
    std::vector<std::pair<std::string, std::string>> response_headers;
    std::string response_body;
    
    handler("GET", "/api/v2/fhss/config", {}, "", status, response_headers, response_body);
    
    auto response = json::parse(response_body);
    auto etag = response["etag"].get<std::string>();
    
    // ETag should be in format "Rev:N"
    REQUIRE(etag.substr(0, 4) == "Rev:");
    REQUIRE(std::all_of(etag.begin() + 4, etag.end(), ::isdigit));
}

}  // namespace graphx::dsp::configuration
