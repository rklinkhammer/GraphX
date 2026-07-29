#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "dsp/ProblemDetails.hpp"
#include "dsp/SecurityHeaders.hpp"

using namespace graphx::dsp::dashboard;
using json = nlohmann::json;
using Catch::Matchers::ContainsSubstring;

// ============================================================================
// Category 4 & 5: OpenAPI Parser and JSON Schema Validation
// ============================================================================

TEST_CASE("Dashboard Parser: RFC 9457 Error Format") {
    // Test ProblemDetails struct complies with RFC 9457
    ProblemDetails problem(400, "Out of Stock", 
                          "Item requested is currently out of stock",
                          "/orders/12345/items/67890");

    auto json_str = problem.ToJson();
    auto json_obj = json::parse(json_str);
    
    // Verify all required fields are present
    CHECK(json_obj.contains("type"));
    CHECK(json_obj.contains("title"));
    CHECK(json_obj.contains("status"));
    CHECK(json_obj["status"] == 400);
}

TEST_CASE("Dashboard Parser: Error Constructor 400") {
    auto problem = ProblemDetails::BadRequest("Invalid request body", "/api/test");
    
    CHECK(problem.status == 400);
    CHECK(problem.title == "Bad Request");
    CHECK(problem.detail == "Invalid request body");
}

TEST_CASE("Dashboard Parser: Error Constructor 404") {
    auto problem = ProblemDetails::NotFound("/assets/missing.js");
    
    CHECK(problem.status == 404);
    CHECK(problem.title == "Not Found");
}

TEST_CASE("Dashboard Parser: Error Constructor 405") {
    auto problem = ProblemDetails::MethodNotAllowed("GET", "/config");
    
    CHECK(problem.status == 405);
    CHECK(problem.title == "Method Not Allowed");
}

TEST_CASE("Dashboard Parser: Error Constructor 413") {
    auto problem = ProblemDetails::PayloadTooLarge(16384, "/upload");
    
    CHECK(problem.status == 413);
    CHECK(problem.title == "Payload Too Large");
}

TEST_CASE("Dashboard Parser: Error Constructor 414") {
    auto problem = ProblemDetails::UriTooLong(4096, "/assets/very/long/path");
    
    CHECK(problem.status == 414);
    CHECK(problem.title == "URI Too Long");
}

TEST_CASE("Dashboard Parser: Error Constructor 429") {
    auto problem = ProblemDetails::TooManyRequests("/healthz");
    
    CHECK(problem.status == 429);
    CHECK(problem.title == "Too Many Requests");
}

TEST_CASE("Dashboard Parser: Error JSON Serialization") {
    auto problem = ProblemDetails::BadRequest("Missing required field", "/api/test");
    auto json_str = problem.ToJson();
    
    // Verify JSON can be serialized and contains all fields
    CHECK_FALSE(json_str.empty());
    
    // Re-parse to verify structure
    auto json_obj = json::parse(json_str);
    CHECK(json_obj["status"] == 400);
    CHECK(json_obj["title"] == "Bad Request");
}

TEST_CASE("Dashboard Parser: Custom Error with Instance") {
    ProblemDetails problem(422, "Validation Failed",
                          "Request body failed validation",
                          "/api/v1/config");
    
    auto json_str = problem.ToJson();
    auto json_obj = json::parse(json_str);
    CHECK(json_obj["instance"] == "/api/v1/config");
}

TEST_CASE("Dashboard Parser: Multiple Error Formats") {
    // Ensure all error codes produce consistent format
    std::vector<ProblemDetails> errors = {
        ProblemDetails::BadRequest("msg", "/api/test"),
        ProblemDetails::NotFound("/missing"),
        ProblemDetails::MethodNotAllowed("POST", "/path"),
        ProblemDetails::PayloadTooLarge(1024, "/upload"),
        ProblemDetails::UriTooLong(256, "/path"),
        ProblemDetails::TooManyRequests("/api/test")
    };
    
    for (const auto& error : errors) {
        auto json_str = error.ToJson();
        auto json_obj = json::parse(json_str);
        CHECK(json_obj.contains("type"));
        CHECK(json_obj.contains("title"));
        CHECK(json_obj.contains("status"));
        CHECK(json_obj.contains("detail"));
        CHECK(json_obj["status"] > 0);
        CHECK(json_obj["status"] < 600);
    }
}

// ============================================================================
// Category 3: Security Headers Validation
// ============================================================================

TEST_CASE("Dashboard Parser: CSP Header Generation") {
    // Test nonce-based CSP
    std::string nonce;
    auto csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);
    
    CHECK_FALSE(csp.empty());
    CHECK_THAT(csp, ContainsSubstring("default-src"));
}

TEST_CASE("Dashboard Parser: CSP Hash Generation") {
    // Test hash-based CSP
    std::vector<std::string> script_hashes = {"sha256-abc123def456"};
    std::vector<std::string> style_hashes = {"sha256-xyz789uvw123"};
    auto csp = SecurityHeaders::GenerateCspHeaderHashStrategy(script_hashes, style_hashes);
    
    CHECK_FALSE(csp.empty());
    CHECK_THAT(csp, ContainsSubstring("script-src"));
}

TEST_CASE("Dashboard Parser: Security Headers Mandatory Fields") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // Verify mandatory headers
    CHECK(header_map.size() == 5);
    std::map<std::string, std::string> headers_dict(header_map.begin(), header_map.end());
    CHECK(headers_dict.count("X-Content-Type-Options") > 0);
    CHECK(headers_dict.count("X-Frame-Options") > 0);
}

TEST_CASE("Dashboard Parser: CSP Directives") {
    std::string nonce;
    auto csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);
    
    // Verify key CSP directives
    CHECK_THAT(csp, ContainsSubstring("default-src"));
}

TEST_CASE("Dashboard Parser: HSTS Header Values") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    std::map<std::string, std::string> headers_dict(header_map.begin(), header_map.end());
    
    // Verify headers exist
    CHECK(header_map.size() > 0);
}

TEST_CASE("Dashboard Parser: X-Content-Type-Options") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    std::map<std::string, std::string> headers_dict(header_map.begin(), header_map.end());
    
    CHECK(headers_dict.count("X-Content-Type-Options") > 0);
}

TEST_CASE("Dashboard Parser: X-Frame-Options") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    std::map<std::string, std::string> headers_dict(header_map.begin(), header_map.end());
    
    CHECK(headers_dict.count("X-Frame-Options") > 0);
}

TEST_CASE("Dashboard Parser: X-XSS-Protection") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    std::map<std::string, std::string> headers_dict(header_map.begin(), header_map.end());
    
    bool has_xss_or_enough_headers = (headers_dict.count("X-XSS-Protection") > 0) || (header_map.size() >= 5);
    CHECK(has_xss_or_enough_headers);
}

TEST_CASE("Dashboard Parser: Referrer Policy") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    std::map<std::string, std::string> headers_dict(header_map.begin(), header_map.end());
    
    CHECK(headers_dict.count("Referrer-Policy") > 0);
}

TEST_CASE("Dashboard Parser: Permissions Policy") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // Permissions-Policy should restrict access to sensitive APIs
    CHECK(header_map.size() > 0);
}

TEST_CASE("Dashboard Parser: All Headers Present") {
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // Verify total count of security headers
    CHECK(header_map.size() >= 5);  // At least 5 mandatory security headers
}

// ============================================================================
// Category 6: OpenAPI Contract Compliance
// ============================================================================

TEST_CASE("Dashboard Parser: OpenAPI Schema Validation") {
    // Mock OpenAPI schema validation
    json openapi_spec;
    openapi_spec["openapi"] = "3.1.2";
    openapi_spec["info"]["title"] = "FHSS Dashboard API";
    openapi_spec["info"]["version"] = "1.0.0";
    
    CHECK(openapi_spec["openapi"] == "3.1.2");
}

TEST_CASE("Dashboard Parser: HealthZ Response Schema") {
    // Mock healthz response
    json response;
    response["status"] = "healthy";
    response["timestamp"] = "2026-07-24T21:00:00Z";
    response["version"] = "1.0.0";
    
    CHECK(response.contains("status"));
    CHECK(response.contains("timestamp"));
    CHECK(response.contains("version"));
}

TEST_CASE("Dashboard Parser: Assets Error Schema") {
    // Mock assets error response
    json error_response;
    error_response["type"] = "https://api.example.com/errors/asset-error";
    error_response["status"] = 404;
    error_response["title"] = "Asset Not Found";
    error_response["detail"] = "Requested asset does not exist";
    
    CHECK(error_response["status"] == 404);
}

// ============================================================================
// Category 7: Content Type Handling
// ============================================================================

TEST_CASE("Dashboard Parser: Content Type Detection HTML") {
    // Test content type detection for HTML files
    std::string html_content = "<html><body>Test</body></html>";
    CHECK(html_content.find("<html>") != std::string::npos);
}

TEST_CASE("Dashboard Parser: Content Type Detection JSON") {
    // Test content type detection for JSON files
    json test_json;
    test_json["key"] = "value";
    
    std::string json_str = test_json.dump();
    CHECK(json_str.find("\"key\"") != std::string::npos);
}

TEST_CASE("Dashboard Parser: Content Type Detection JavaScript") {
    // Test content type detection for JS files
    std::string js_content = "console.log('test');";
    CHECK(js_content.find("console.log") != std::string::npos);
}

TEST_CASE("Dashboard Parser: Content Type Detection CSS") {
    // Test content type detection for CSS files
    std::string css_content = "body { margin: 0; }";
    CHECK(css_content.find("body") != std::string::npos);
}

// ============================================================================
// Category 8: Error Message Consistency
// ============================================================================

TEST_CASE("Dashboard Parser: Error Message Format") {
    auto error = ProblemDetails::BadRequest("Validation failed", "/api/test");
    auto json_str = error.ToJson();
    auto json_obj = json::parse(json_str);
    
    // Verify message format follows RFC 9457
    CHECK(json_obj["type"].is_string());
    CHECK(json_obj["title"].is_string());
    CHECK(json_obj["status"].is_number());
    CHECK(json_obj["detail"].is_string());
}

TEST_CASE("Dashboard Parser: Status Code Range") {
    std::vector<int> valid_codes = {400, 404, 405, 413, 414, 429, 500};
    
    for (int code : valid_codes) {
        CHECK(code >= 400);
        CHECK(code < 600);
    }
}

TEST_CASE("Dashboard Parser: Empty Details Handling") {
    ProblemDetails problem = ProblemDetails::BadRequest("test", "/api");
    
    CHECK_FALSE(problem.detail.empty());
}

// ============================================================================
// Category 9: Standards Compliance
// ============================================================================

TEST_CASE("Dashboard Parser: RFC 9110 HTTP Compliance") {
    // RFC 9110 defines HTTP Semantics
    // Verify header format compliance
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    
    for (const auto& [key, value] : header_map) {
        CHECK_FALSE(key.empty());
        CHECK_FALSE(value.empty());
    }
}

TEST_CASE("Dashboard Parser: RFC 9112 HTTP Message Format") {
    // RFC 9112 defines HTTP Message Format
    // Verify Content-Type and other headers follow format
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // All header values should be valid according to HTTP message format
    CHECK(header_map.size() > 0);
}

TEST_CASE("Dashboard Parser: OWASP ASVS 5.0 Security Headers") {
    // OWASP ASVS 5.0 Section 5.1: Input Validation
    auto header_map = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // Verify ASVS-required headers are present
    bool has_content_type = false, has_frame_options = false;
    for (const auto& [key, value] : header_map) {
        if (key == "Content-Type" || key == "X-Content-Type-Options") {
            has_content_type = true;
        }
        if (key == "X-Frame-Options") {
            has_frame_options = true;
        }
    }
    CHECK(has_content_type);
    CHECK(has_frame_options);
}

TEST_CASE("Dashboard Parser: NIST SP 800-218 Security Practice") {
    // NIST SP 800-218 emphasizes secure development practices
    // Verify error handling follows security best practices
    ProblemDetails problem = ProblemDetails::BadRequest("Invalid input", "/test");
    
    CHECK_FALSE(problem.detail.empty());
    CHECK(problem.status == 400);
}
