#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>

#include "dsp/SecurityHeaders.hpp"

using Catch::Matchers::ContainsSubstring;

using namespace graphx::dsp::dashboard;
using Catch::Matchers::Contains;

// ============================================================================
// Category 3: Security Headers & CSP
// ============================================================================

TEST_CASE("SecurityHeaders: Hash-Based CSP Generation") {
    std::vector<std::string> script_hashes = {"sha256-abc123", "sha256-def456"};
    std::vector<std::string> style_hashes = {"sha256-ghi789"};
    
    std::string csp =
        SecurityHeaders::GenerateCspHeaderHashStrategy(script_hashes, style_hashes);
    
    // Verify CSP contains all required directives
    REQUIRE_THAT(csp, Contains("default-src 'none'"));
    REQUIRE_THAT(csp, Contains("script-src 'self'"));
    REQUIRE_THAT(csp, Contains("sha256-abc123"));
    REQUIRE_THAT(csp, Contains("sha256-def456"));
    REQUIRE_THAT(csp, Contains("style-src 'self'"));
    REQUIRE_THAT(csp, Contains("sha256-ghi789"));
    REQUIRE_THAT(csp, Contains("connect-src 'self'"));
    REQUIRE_THAT(csp, Contains("frame-ancestors 'none'"));
    REQUIRE_THAT(csp, Contains("base-uri 'self'"));
    REQUIRE_THAT(csp, Contains("form-action 'self'"));
}

TEST_CASE("SecurityHeaders: Nonce-Based CSP Generation") {
    std::string nonce;
    std::string csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);
    
    // Verify nonce was generated
    REQUIRE_FALSE(nonce.empty());
    
    // Verify CSP contains nonce
    REQUIRE_THAT(csp, Contains("nonce-" + nonce));
    
    // Verify CSP contains required directives
    REQUIRE_THAT(csp, Contains("default-src 'none'"));
    REQUIRE_THAT(csp, Contains("script-src 'self'"));
    REQUIRE_THAT(csp, Contains("style-src 'self'"));
    REQUIRE_THAT(csp, Contains("connect-src 'self'"));
}

TEST_CASE("SecurityHeaders: Multiple Nonces Are Unique") {
    std::string nonce1, nonce2, nonce3;
    
    SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce1);
    SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce2);
    SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce3);
    
    // Nonces should be different
    REQUIRE(nonce1 != nonce2);
    REQUIRE(nonce2 != nonce3);
    REQUIRE(nonce1 != nonce3);
}

TEST_CASE("SecurityHeaders: Mandatory Headers") {
    auto headers = SecurityHeaders::GetMandatorySecurityHeaders();
    
    // Should have 5 headers
    REQUIRE(headers.size() == 5);
    
    // Convert to map for easier checking
    std::map<std::string, std::string> header_map(headers.begin(), headers.end());
    
    REQUIRE(header_map["X-Content-Type-Options"] == "nosniff");
    REQUIRE(header_map["X-Frame-Options"] == "DENY");
    REQUIRE(header_map["X-XSS-Protection"] == "1; mode=block");
    REQUIRE(header_map["Referrer-Policy"] == "no-referrer");
    REQUIRE(header_map["Permissions-Policy"] ==
            "camera=(), microphone=(), geolocation=()");
}

TEST_CASE("SecurityHeaders: Nonce Generation") {
    // Generate multiple nonces
    std::string nonce1 = SecurityHeaders::GenerateNonce();
    std::string nonce2 = SecurityHeaders::GenerateNonce();
    
    // Nonces should be non-empty
    REQUIRE_FALSE(nonce1.empty());
    REQUIRE_FALSE(nonce2.empty());
    
    // Nonces should be different
    REQUIRE(nonce1 != nonce2);
    
    // Nonces should be base64-encoded (alphanumeric + /+=)
    for (char c : nonce1) {
        REQUIRE(
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=');
    }
}

TEST_CASE("SecurityHeaders: SHA256 Hash Computation") {
    std::string content = "alert('XSS');";
    std::string hash = SecurityHeaders::ComputeSha256Hash(content);
    
    // Hash should start with sha256-
    REQUIRE_THAT(hash, Contains("sha256-"));
    
    // Hash should be the same for identical content
    std::string hash2 = SecurityHeaders::ComputeSha256Hash(content);
    REQUIRE(hash == hash2);
    
    // Different content should produce different hash
    std::string hash3 = SecurityHeaders::ComputeSha256Hash("different content");
    REQUIRE(hash != hash3);
}

TEST_CASE("SecurityHeaders: CSP Validation") {
    std::string valid_csp = "default-src 'none'; script-src 'self'";
    std::string invalid_csp = "";
    
    REQUIRE(SecurityHeaders::ValidateCspHeader(valid_csp));
    REQUIRE_FALSE(SecurityHeaders::ValidateCspHeader(invalid_csp));
}

TEST_CASE("SecurityHeaders: No Unsafe Directives in Hash CSP") {
    std::vector<std::string> script_hashes = {"sha256-abc123"};
    std::vector<std::string> style_hashes;
    
    std::string csp =
        SecurityHeaders::GenerateCspHeaderHashStrategy(script_hashes, style_hashes);
    
    // Hash-based CSP should not contain unsafe directives
    REQUIRE(SecurityHeaders::VerifyNoUnsafeDirectives(csp));
}

TEST_CASE("SecurityHeaders: No Unsafe Directives in Nonce CSP") {
    std::string nonce;
    std::string csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);
    
    // Nonce-based CSP should not contain unsafe directives
    REQUIRE(SecurityHeaders::VerifyNoUnsafeDirectives(csp));
}

TEST_CASE("SecurityHeaders: Detect Unsafe-Inline") {
    std::string unsafe_csp = "default-src 'none'; script-src 'unsafe-inline'";
    
    // Should detect unsafe-inline
    REQUIRE_FALSE(SecurityHeaders::VerifyNoUnsafeDirectives(unsafe_csp));
}

TEST_CASE("SecurityHeaders: Detect Unsafe-Eval") {
    std::string unsafe_csp = "default-src 'none'; script-src 'unsafe-eval'";
    
    // Should detect unsafe-eval
    REQUIRE_FALSE(SecurityHeaders::VerifyNoUnsafeDirectives(unsafe_csp));
}
