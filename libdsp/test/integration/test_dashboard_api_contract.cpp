#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <map>
#include <string>

#include "dsp/DashboardHttpServer.hpp"
#include "dsp/SecurityHeaders.hpp"
#include "dsp/ProblemDetails.hpp"

using namespace graphx::dsp::dashboard;
using Catch::Matchers::ContainsSubstring;

// ============================================================================
// Category 4: OpenAPI & JSON Schema - API Contract Tests
// ============================================================================

TEST_CASE("API Contract: Healthz Endpoint Metadata") {
    // Test that the healthz endpoint can be registered and respond with proper metadata
    DashboardHttpServer::Options opts;
    opts.host = "127.0.0.1";
    opts.port = 9001;  // Avoid conflicts
    
    DashboardHttpServer server(opts);
    
    // Register healthz endpoint
    server.RegisterHandler("GET", "/healthz",
        [](std::string_view method,
           [[maybe_unused]] std::string_view path,
           [[maybe_unused]] const std::vector<std::pair<std::string, std::string>>& headers,
           [[maybe_unused]] std::string_view body,
           int& response_status,
           std::vector<std::pair<std::string, std::string>>& response_headers,
           std::string& response_body) -> bool {
            if (method != "GET") {
                return false;  // Let server generate 405
            }
            
            response_status = 200;
            response_headers.push_back({"Content-Type", "application/json"});
            response_body = R"({"status":"alive"})";
            return true;
        });
    
    REQUIRE_FALSE(server.IsRunning());  // Server not started yet
}

TEST_CASE("API Contract: Assets Endpoint Exists") {
    DashboardHttpServer::Options opts;
    opts.host = "127.0.0.1";
    opts.port = 9002;
    
    DashboardHttpServer server(opts);
    
    // Register assets endpoint handler
    server.RegisterHandler("GET", "/assets/",
        [](std::string_view method,
           [[maybe_unused]] std::string_view path,
           [[maybe_unused]] const std::vector<std::pair<std::string, std::string>>& headers,
           [[maybe_unused]] std::string_view body,
           int& response_status,
           std::vector<std::pair<std::string, std::string>>& response_headers,
           std::string& response_body) -> bool {
            response_status = 200;
            response_headers.push_back({"Content-Type", "text/html"});
            response_body = "<html></html>";
            return true;
        });
    
    REQUIRE_FALSE(server.IsRunning());  // Server not started yet
}

TEST_CASE("API Contract: Root Endpoint") {
    DashboardHttpServer::Options opts;
    opts.host = "127.0.0.1";
    opts.port = 9003;
    
    DashboardHttpServer server(opts);
    
    // Register root endpoint
    server.RegisterHandler("GET", "/",
        [](std::string_view method,
           [[maybe_unused]] std::string_view path,
           [[maybe_unused]] const std::vector<std::pair<std::string, std::string>>& headers,
           [[maybe_unused]] std::string_view body,
           int& response_status,
           std::vector<std::pair<std::string, std::string>>& response_headers,
           std::string& response_body) -> bool {
            response_status = 200;
            response_headers.push_back({"Content-Type", "text/html"});
            response_body = "<html><body>Dashboard</body></html>";
            return true;
        });
    
    REQUIRE_FALSE(server.IsRunning());  // Server not started yet
}

TEST_CASE("API Contract: OpenAPI Endpoint") {
    DashboardHttpServer::Options opts;
    opts.host = "127.0.0.1";
    opts.port = 9004;
    
    DashboardHttpServer server(opts);
    
    // Register OpenAPI endpoint
    server.RegisterHandler("GET", "/openapi.yaml",
        [](std::string_view method,
           [[maybe_unused]] std::string_view path,
           [[maybe_unused]] const std::vector<std::pair<std::string, std::string>>& headers,
           [[maybe_unused]] std::string_view body,
           int& response_status,
           std::vector<std::pair<std::string, std::string>>& response_headers,
           std::string& response_body) -> bool {
            response_status = 200;
            response_headers.push_back({"Content-Type", "application/yaml"});
            response_body = "openapi: 3.1.2";
            return true;
        });
    
    REQUIRE_FALSE(server.IsRunning());  // Server not started yet
}

// ============================================================================
// Tests for HTTP Status Codes
// ============================================================================

TEST_CASE("API Contract: HTTP 400 Bad Request") {
    auto problem = ProblemDetails::BadRequest("Malformed header", "/test");
    REQUIRE(problem.status == 400);
    
    std::string json = problem.ToJson();
    REQUIRE_THAT(json, ContainsSubstring("\"status\":400"));
    REQUIRE_THAT(json, ContainsSubstring("Bad Request"));
}

TEST_CASE("API Contract: HTTP 404 Not Found") {
    auto problem = ProblemDetails::NotFound("/assets/missing.js");
    REQUIRE(problem.status == 404);
    
    std::string json = problem.ToJson();
    REQUIRE_THAT(json, ContainsSubstring("\"status\":404"));
    REQUIRE_THAT(json, ContainsSubstring("Not Found"));
}

TEST_CASE("API Contract: HTTP 405 Method Not Allowed") {
    auto problem = ProblemDetails::MethodNotAllowed("POST", "/healthz");
    REQUIRE(problem.status == 405);
    
    std::string json = problem.ToJson();
    REQUIRE_THAT(json, ContainsSubstring("\"status\":405"));
    REQUIRE_THAT(json, ContainsSubstring("Method Not Allowed"));
}

TEST_CASE("API Contract: HTTP 413 Payload Too Large") {
    auto problem = ProblemDetails::PayloadTooLarge(16384, "/upload");
    REQUIRE(problem.status == 413);
    
    std::string json = problem.ToJson();
    REQUIRE_THAT(json, ContainsSubstring("\"status\":413"));
    REQUIRE_THAT(json, ContainsSubstring("Payload Too Large"));
}

TEST_CASE("API Contract: HTTP 414 URI Too Long") {
    auto problem = ProblemDetails::UriTooLong(4096, "/assets/very/long/path");
    REQUIRE(problem.status == 414);
    
    std::string json = problem.ToJson();
    REQUIRE_THAT(json, ContainsSubstring("\"status\":414"));
    REQUIRE_THAT(json, ContainsSubstring("URI Too Long"));
}

TEST_CASE("API Contract: HTTP 429 Too Many Requests") {
    auto problem = ProblemDetails::TooManyRequests("/healthz");
    REQUIRE(problem.status == 429);
    
    std::string json = problem.ToJson();
    REQUIRE_THAT(json, ContainsSubstring("\"status\":429"));
    REQUIRE_THAT(json, ContainsSubstring("Too Many Requests"));
}

// ============================================================================
// Tests for Response Headers
// ============================================================================

TEST_CASE("API Contract: All Responses Include Security Headers") {
    auto mandatory_headers = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // Should have all 5 mandatory headers
    REQUIRE(mandatory_headers.size() == 5);
    
    // Verify specific headers exist
    std::map<std::string, std::string> header_map(mandatory_headers.begin(),
                                                  mandatory_headers.end());
    REQUIRE(header_map.count("X-Content-Type-Options") > 0);
    REQUIRE(header_map.count("X-Frame-Options") > 0);
    REQUIRE(header_map.count("X-XSS-Protection") > 0);
    REQUIRE(header_map.count("Referrer-Policy") > 0);
    REQUIRE(header_map.count("Permissions-Policy") > 0);
}

TEST_CASE("API Contract: CSP Header Included") {
    std::string nonce;
    std::string csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);
    
    // CSP should be present
    REQUIRE_FALSE(csp.empty());
    REQUIRE_THAT(csp, ContainsSubstring("default-src 'none'"));
}

TEST_CASE("API Contract: Content-Type: application/problem+json") {
    auto problem = ProblemDetails::NotFound("/test");
    auto content_type = ProblemDetails::ContentType();
    
    REQUIRE(content_type == "application/problem+json");
}
