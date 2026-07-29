#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>

#include "dsp/ProblemDetails.hpp"

using Catch::Matchers::ContainsSubstring;

using namespace graphx::dsp::dashboard;
using Catch::Matchers::Contains;

// ============================================================================
// Category 5: Error Format (RFC 9457)
// ============================================================================

TEST_CASE("ProblemDetails: Constructor") {
    ProblemDetails problem(404, "Not Found", "The requested resource was not found",
                           "/assets/missing.js");
    
    REQUIRE(problem.status == 404);
    REQUIRE(problem.title == "Not Found");
    REQUIRE(problem.detail == "The requested resource was not found");
    REQUIRE(problem.instance == "/assets/missing.js");
}

TEST_CASE("ProblemDetails: BadRequest Factory") {
    ProblemDetails problem = ProblemDetails::BadRequest("Invalid header", "/api/test");
    
    REQUIRE(problem.status == 400);
    REQUIRE(problem.title == "Bad Request");
    REQUIRE(problem.detail == "Invalid header");
    REQUIRE(problem.instance == "/api/test");
}

TEST_CASE("ProblemDetails: NotFound Factory") {
    ProblemDetails problem = ProblemDetails::NotFound("/assets/missing.html");
    
    REQUIRE(problem.status == 404);
    REQUIRE(problem.title == "Not Found");
    REQUIRE(problem.instance == "/assets/missing.html");
}

TEST_CASE("ProblemDetails: MethodNotAllowed Factory") {
    ProblemDetails problem = ProblemDetails::MethodNotAllowed("POST", "/healthz");
    
    REQUIRE(problem.status == 405);
    REQUIRE(problem.title == "Method Not Allowed");
    REQUIRE_THAT(problem.detail, Contains("POST"));
}

TEST_CASE("ProblemDetails: PayloadTooLarge Factory") {
    ProblemDetails problem =
        ProblemDetails::PayloadTooLarge(1024 * 1024, "/upload");
    
    REQUIRE(problem.status == 413);
    REQUIRE(problem.title == "Payload Too Large");
    REQUIRE_THAT(problem.detail, Contains("1048576"));
}

TEST_CASE("ProblemDetails: UriTooLong Factory") {
    ProblemDetails problem = ProblemDetails::UriTooLong(4096, "/assets/very/long/path");
    
    REQUIRE(problem.status == 414);
    REQUIRE(problem.title == "URI Too Long");
    REQUIRE_THAT(problem.detail, Contains("4096"));
}

TEST_CASE("ProblemDetails: TooManyRequests Factory") {
    ProblemDetails problem = ProblemDetails::TooManyRequests("/healthz");
    
    REQUIRE(problem.status == 429);
    REQUIRE(problem.title == "Too Many Requests");
    REQUIRE(problem.instance == "/healthz");
}

TEST_CASE("ProblemDetails: PreconditionFailed Factory") {
    ProblemDetails problem = ProblemDetails::PreconditionFailed("/api/data");
    
    REQUIRE(problem.status == 412);
    REQUIRE(problem.title == "Precondition Failed");
    REQUIRE(problem.instance == "/api/data");
}

TEST_CASE("ProblemDetails: JSON Serialization") {
    ProblemDetails problem = ProblemDetails::NotFound("/assets/missing.js");
    std::string json = problem.ToJson();
    
    // Should be valid JSON
    REQUIRE_THAT(json, Contains("\"type\":"));
    REQUIRE_THAT(json, Contains("\"title\":"));
    REQUIRE_THAT(json, Contains("\"status\":"));
    REQUIRE_THAT(json, Contains("\"detail\":"));
    REQUIRE_THAT(json, Contains("\"instance\":"));
    
    // Should contain the values
    REQUIRE_THAT(json, Contains("404"));
    REQUIRE_THAT(json, Contains("Not Found"));
    REQUIRE_THAT(json, Contains("/assets/missing.js"));
}

TEST_CASE("ProblemDetails: JSON Escaping") {
    // Test with special characters that need escaping
    ProblemDetails problem(400, "Bad Request", "Contains \" and \\ and\nnewline",
                           "/test");
    std::string json = problem.ToJson();
    
    // Should have escaped quotes
    REQUIRE_THAT(json, Contains("\\\""));
    
    // Should have escaped newlines
    REQUIRE_THAT(json, Contains("\\n"));
}

TEST_CASE("ProblemDetails: No File Path Leakage") {
    // Ensure errors don't expose file paths
    ProblemDetails problem =
        ProblemDetails::BadRequest("Request malformed", "/assets/../../etc/passwd");
    
    std::string json = problem.ToJson();
    
    // Should contain instance path but not system paths
    REQUIRE_THAT(json, Contains("/assets/../../etc/passwd"));
    REQUIRE_THAT(json, !Contains("/etc/passwd"));
}

TEST_CASE("ProblemDetails: Content-Type Header") {
    auto content_type = ProblemDetails::ContentType();
    REQUIRE(content_type == "application/problem+json");
}

TEST_CASE("ProblemDetails: Generic Error Messages") {
    // Test that errors use generic messages without revealing system details
    ProblemDetails error404 = ProblemDetails::NotFound("/missing");
    ProblemDetails error400 = ProblemDetails::BadRequest("Validation failed", "/api");
    
    // Should not expose implementation details
    REQUIRE_FALSE(error404.detail.find("/etc") != std::string::npos);
    REQUIRE_FALSE(error400.detail.find("stack") != std::string::npos);
}
