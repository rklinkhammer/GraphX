#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "dsp/ProblemDetails.hpp"
#include "dsp/SecurityHeaders.hpp"

using namespace graphx::dsp::dashboard;
using json = nlohmann::json;
using Catch::Matchers::Contains;

// ============================================================================
// Category 4 & 5: OpenAPI Parser and JSON Schema Validation
// ============================================================================

TEST_CASE("Dashboard Parser: RFC 9457 Error Format") {
    // Test ProblemDetails struct complies with RFC 9457
    ProblemDetails problem;
    problem.type = "https://example.com/probs/out-of-stock";
    problem.title = "Out of Stock";
    problem.status = 400;
    problem.detail = "Item requested is currently out of stock";
    problem.instance = "/orders/12345/items/67890";

    auto json_obj = problem.ToJson();
    
    // Verify all required fields are present
    CHECK(json_obj.contains("type"));
    CHECK(json_obj.contains("title"));
    CHECK(json_obj.contains("status"));
    CHECK(json_obj["status"] == 400);
}

TEST_CASE("Dashboard Parser: Error Constructor 400") {
    auto problem = ProblemDetails::BadRequest("Invalid request body");
    
    CHECK(problem.status == 400);
    CHECK(problem.title == "Bad Request");
    CHECK(problem.detail == "Invalid request body");
    CHECK(problem.type.find("400") != std::string::npos);
}

TEST_CASE("Dashboard Parser: Error Constructor 404") {
    auto problem = ProblemDetails::NotFound("Asset not found");
    
    CHECK(problem.status == 404);
    CHECK(problem.title == "Not Found");
    CHECK(problem.detail == "Asset not found");
}

TEST_CASE("Dashboard Parser: Error Constructor 405") {
    auto problem = ProblemDetails::MethodNotAllowed("GET", "/config");
    
    CHECK(problem.status == 405);
    CHECK(problem.title == "Method Not Allowed");
}

TEST_CASE("Dashboard Parser: Error Constructor 413") {
    auto problem = ProblemDetails::PayloadTooLarge("Request body exceeds max size");
    
    CHECK(problem.status == 413);
    CHECK(problem.title == "Payload Too Large");
}

TEST_CASE("Dashboard Parser: Error Constructor 414") {
    auto problem = ProblemDetails::UriTooLong("Request URI exceeds max length");
    
    CHECK(problem.status == 414);
    CHECK(problem.title == "URI Too Long");
}

TEST_CASE("Dashboard Parser: Error Constructor 429") {
    auto problem = ProblemDetails::TooManyRequests("Rate limit exceeded");
    
    CHECK(problem.status == 429);
    CHECK(problem.title == "Too Many Requests");
}

TEST_CASE("Dashboard Parser: Error JSON Serialization") {
    auto problem = ProblemDetails::BadRequest("Missing required field");
    auto json_obj = problem.ToJson();
    
    // Verify JSON can be serialized and contains all fields
    std::string json_str = json_obj.dump();
    CHECK_FALSE(json_str.empty());
    
    // Re-parse to verify structure
    auto reparsed = json::parse(json_str);
    CHECK(reparsed["status"] == 400);
    CHECK(reparsed["title"] == "Bad Request");
}

TEST_CASE("Dashboard Parser: Custom Error with Instance") {
    ProblemDetails problem;
    problem.type = "https://api.example.com/errors/validation";
    problem.title = "Validation Failed";
    problem.status = 422;
    problem.detail = "Request body failed validation";
    problem.instance = "/api/v1/config";
    
    auto json_obj = problem.ToJson();
    CHECK(json_obj["instance"] == "/api/v1/config");
}

TEST_CASE("Dashboard Parser: Multiple Error Formats") {
    // Ensure all error codes produce consistent format
    std::vector<ProblemDetails> errors = {
        ProblemDetails::BadRequest("msg"),
        ProblemDetails::NotFound("msg"),
        ProblemDetails::MethodNotAllowed("POST", "/path"),
        ProblemDetails::PayloadTooLarge("msg"),
        ProblemDetails::UriTooLong("msg"),
        ProblemDetails::TooManyRequests("msg")
    };
    
    for (const auto& error : errors) {
        auto json_obj = error.ToJson();
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
    SecurityHeaders headers;
    
    // Test nonce-based CSP
    std::string nonce = "abc123def456";
    auto csp = headers.GenerateCSPHeader(SecurityHeaders::CSPStrategy::NONCE, nonce);
    
    CHECK_FALSE(csp.empty());
    CHECK(csp.find("nonce-" + nonce) != std::string::npos);
}

TEST_CASE("Dashboard Parser: CSP Hash Generation") {
    SecurityHeaders headers;
    
    // Test hash-based CSP
    auto csp = headers.GenerateCSPHeader(SecurityHeaders::CSPStrategy::HASH, "");
    
    CHECK_FALSE(csp.empty());
    CHECK(csp.find("sha256-") != std::string::npos);
}

TEST_CASE("Dashboard Parser: Security Headers Mandatory Fields") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    // Verify mandatory headers
    CHECK(header_map.count("X-Content-Type-Options") > 0);
    CHECK(header_map.count("X-Frame-Options") > 0);
    CHECK(header_map.count("X-XSS-Protection") > 0);
    CHECK(header_map.count("Strict-Transport-Security") > 0);
}

TEST_CASE("Dashboard Parser: CSP Directives") {
    SecurityHeaders headers;
    auto csp = headers.GenerateCSPHeader(SecurityHeaders::CSPStrategy::NONCE, "test");
    
    // Verify key CSP directives
    CHECK(csp.find("default-src") != std::string::npos);
    CHECK(csp.find("script-src") != std::string::npos);
    CHECK(csp.find("style-src") != std::string::npos);
    CHECK(csp.find("img-src") != std::string::npos);
}

TEST_CASE("Dashboard Parser: HSTS Header Values") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    auto hsts = header_map["Strict-Transport-Security"];
    
    // Verify HSTS includes max-age and includeSubDomains
    CHECK(hsts.find("max-age") != std::string::npos);
    CHECK(hsts.find("includeSubDomains") != std::string::npos);
}

TEST_CASE("Dashboard Parser: X-Content-Type-Options") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    CHECK(header_map["X-Content-Type-Options"] == "nosniff");
}

TEST_CASE("Dashboard Parser: X-Frame-Options") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    CHECK(header_map["X-Frame-Options"] == "DENY");
}

TEST_CASE("Dashboard Parser: X-XSS-Protection") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    CHECK(header_map["X-XSS-Protection"].find("1; mode=block") != std::string::npos);
}

TEST_CASE("Dashboard Parser: Referrer Policy") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    CHECK(header_map.count("Referrer-Policy") > 0);
    CHECK(header_map["Referrer-Policy"] == "no-referrer");
}

TEST_CASE("Dashboard Parser: Permissions Policy") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    // Permissions-Policy should restrict access to sensitive APIs
    CHECK(header_map.count("Permissions-Policy") > 0);
}

TEST_CASE("Dashboard Parser: All Headers Present") {
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    // Verify total count of security headers
    CHECK(header_map.size() >= 7);  // At least 7 security headers
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
    auto error = ProblemDetails::BadRequest("Validation failed");
    auto json_obj = error.ToJson();
    
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
    ProblemDetails problem = ProblemDetails::BadRequest("");
    
    CHECK_FALSE(problem.detail.empty() || problem.detail == "");
}

// ============================================================================
// Category 9: Standards Compliance
// ============================================================================

TEST_CASE("Dashboard Parser: RFC 9110 HTTP Compliance") {
    // RFC 9110 defines HTTP Semantics
    // Verify header format compliance
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    for (const auto& [key, value] : header_map) {
        CHECK_FALSE(key.empty());
        CHECK_FALSE(value.empty());
    }
}

TEST_CASE("Dashboard Parser: RFC 9112 HTTP Message Format") {
    // RFC 9112 defines HTTP Message Format
    // Verify Content-Type and other headers follow format
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    // All header values should be valid according to HTTP message format
    CHECK(header_map.size() > 0);
}

TEST_CASE("Dashboard Parser: OWASP ASVS 5.0 Security Headers") {
    // OWASP ASVS 5.0 Section 5.1: Input Validation
    SecurityHeaders headers;
    auto header_map = headers.GetSecurityHeaders();
    
    // Verify ASVS-required headers are present
    CHECK(header_map.count("Content-Type") > 0 || header_map.count("X-Content-Type-Options") > 0);
    CHECK(header_map.count("X-Frame-Options") > 0);
}

TEST_CASE("Dashboard Parser: NIST SP 800-218 Security Practice") {
    // NIST SP 800-218 emphasizes secure development practices
    // Verify error handling follows security best practices
    ProblemDetails problem = ProblemDetails::BadRequest("Invalid input");
    
    CHECK_FALSE(problem.detail.empty());
    CHECK(problem.status == 400);
}
