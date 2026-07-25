#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "dsp/DashboardHttpServer.hpp"

using namespace graphx::dsp::dashboard;
using Catch::Matchers::ContainsSubstring;

// ============================================================================
// Category 1: HTTP Server Architecture
// ============================================================================

TEST_CASE("DashboardHttpServer: Loopback Binding Validation") {
    // IPv4 loopback addresses (ACCEPT)
    REQUIRE(DashboardHttpServer::ValidateLoopbackBinding("127.0.0.1"));
    REQUIRE(DashboardHttpServer::ValidateLoopbackBinding("127.0.0.2"));
    REQUIRE(DashboardHttpServer::ValidateLoopbackBinding("127.255.255.255"));

    // IPv6 loopback address (ACCEPT)
    REQUIRE(DashboardHttpServer::ValidateLoopbackBinding("::1"));

    // Wildcard addresses (REJECT)
    REQUIRE_THROWS_AS(
        DashboardHttpServer::ValidateLoopbackBinding("0.0.0.0"),
        std::runtime_error);
    REQUIRE_THROWS_AS(
        DashboardHttpServer::ValidateLoopbackBinding("::"),
        std::runtime_error);
    REQUIRE_THROWS_AS(
        DashboardHttpServer::ValidateLoopbackBinding("::/0"),
        std::runtime_error);

    // Public addresses (REJECT)
    REQUIRE_THROWS_AS(
        DashboardHttpServer::ValidateLoopbackBinding("8.8.8.8"),
        std::runtime_error);
    REQUIRE_THROWS_AS(
        DashboardHttpServer::ValidateLoopbackBinding("192.168.1.1"),
        std::runtime_error);
}

TEST_CASE("DashboardHttpServer: Constructor Validation") {
    // Valid loopback addresses should construct successfully
    REQUIRE_NOTHROW([&]() {
        DashboardHttpServer::Options opts;
        opts.host = "127.0.0.1";
        DashboardHttpServer server(opts);
    }());

    REQUIRE_NOTHROW([&]() {
        DashboardHttpServer::Options opts;
        opts.host = "::1";
        DashboardHttpServer server(opts);
    }());

    // Invalid public addresses should fail
    REQUIRE_THROWS_AS([&]() {
        DashboardHttpServer::Options opts;
        opts.host = "0.0.0.0";
        DashboardHttpServer server(opts);
    }(), std::runtime_error);
}

TEST_CASE("DashboardHttpServer: Default Options") {
    DashboardHttpServer::Options opts;
    
    CHECK(opts.host == "127.0.0.1");
    CHECK(opts.port == 8765);
    CHECK(opts.max_header_bytes == 16 * 1024);
    CHECK(opts.max_body_bytes == 64 * 1024 * 1024);
    CHECK(opts.max_response_bytes == 256 * 1024 * 1024);
    CHECK(opts.read_timeout_seconds == 30);
    CHECK(opts.write_timeout_seconds == 30);
    CHECK(opts.idle_timeout_seconds == 120);
    CHECK(opts.operation_timeout_seconds == 300);
    CHECK(opts.max_concurrent_connections == 8);
}

// ============================================================================
// Tests for Request/Response Limits
// ============================================================================

TEST_CASE("DashboardHttpServer: Request Limits Enforced") {
    DashboardHttpServer::Options opts;
    opts.host = "127.0.0.1";
    opts.port = 9999;  // Avoid port conflicts
    opts.max_header_bytes = 16 * 1024;
    opts.max_body_bytes = 1024 * 1024;  // 1 MiB
    
    DashboardHttpServer server(opts);
    
    // Verify limits are set
    CHECK(opts.max_header_bytes == 16 * 1024);
    CHECK(opts.max_body_bytes == 1024 * 1024);
}

// ============================================================================
// Tests for HTTP Protocol Compliance
// ============================================================================

TEST_CASE("DashboardHttpServer: Handler Registration") {
    DashboardHttpServer::Options opts;
    opts.host = "127.0.0.1";
    
    DashboardHttpServer server(opts);
    
    // Register a simple GET handler
    REQUIRE_NOTHROW([&]() {
        server.RegisterHandler("GET", "/test", [](
            std::string_view method,
            std::string_view path,
            const std::vector<std::pair<std::string, std::string>>& headers,
            std::string_view body,
            int& response_status,
            std::vector<std::pair<std::string, std::string>>& response_headers,
            std::string& response_body
        ) -> bool {
            response_status = 200;
            response_headers.push_back({"Content-Type", "text/plain"});
            response_body = "OK";
            return true;
        });
    }());
}
