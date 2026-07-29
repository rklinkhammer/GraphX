/**
 * @file dashboard-server.cpp
 * @brief FHSS Dashboard Phase 1 HTTP Server
 *
 * Implements loopback-only HTTP server with:
 * - RFC 9110/9112 compliance
 * - Static asset serving with path containment
 * - Security headers and CSP
 * - RFC 9457 error responses
 */

#include "dsp/DashboardHttpServer.hpp"
#include "dsp/AssetResolver.hpp"
#include "dsp/SecurityHeaders.hpp"
#include "dsp/ProblemDetails.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <csignal>
#include <thread>
#include <chrono>

using namespace graphx::dsp::dashboard;
namespace fs = std::filesystem;

// Global server pointer for signal handler
static DashboardHttpServer* g_server = nullptr;

// Signal handler for graceful shutdown
void SignalHandler(int sig) {
    if (g_server) {
        std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
        g_server->Stop();
    }
}

int main(int argc, char* argv[]) {
    try {
        // Parse command-line arguments
        std::string asset_root = "./dist";
        uint16_t port = 8765;
        std::string host = "127.0.0.1";

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--asset-root" && i + 1 < argc) {
                asset_root = argv[++i];
            } else if (arg == "--port" && i + 1 < argc) {
                port = static_cast<uint16_t>(std::stoul(argv[++i]));
            } else if (arg == "--host" && i + 1 < argc) {
                host = argv[++i];
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: graphx-fhss-dashboard-server [options]\n\n"
                          << "Options:\n"
                          << "  --asset-root PATH    Root directory for static assets (default: ./dist)\n"
                          << "  --port PORT          HTTP listen port (default: 8765)\n"
                          << "  --host HOST          HTTP listen address (default: 127.0.0.1)\n"
                          << "  --help               Show this message\n";
                return 0;
            }
        }

        // Validate asset root
        if (!fs::exists(asset_root)) {
            std::cerr << "Error: Asset root does not exist: " << asset_root << std::endl;
            return 1;
        }

        // Configure server
        DashboardHttpServer::Options opts;
        opts.host = host;
        opts.port = port;
        opts.asset_root_path = asset_root;

        // Create server
        DashboardHttpServer server(opts);
        g_server = &server;

        // Register signal handlers
        std::signal(SIGTERM, SignalHandler);
        std::signal(SIGINT, SignalHandler);

        // Create asset resolver
        AssetResolver::Options resolver_opts;
        resolver_opts.asset_root = asset_root;
        resolver_opts.allow_symlinks = false;
        AssetResolver asset_resolver(resolver_opts);

        // Register health check endpoint
        server.RegisterHandler("GET", "/healthz",
            [](std::string_view method,
               std::string_view path,
               const std::vector<std::pair<std::string, std::string>>& headers,
               std::string_view body,
               int& response_status,
               std::vector<std::pair<std::string, std::string>>& response_headers,
               std::string& response_body) -> bool {
                if (method != "GET") {
                    return false;  // Let server generate 405
                }

                // Generate security headers
                auto security_headers = SecurityHeaders::GetMandatorySecurityHeaders();
                std::string nonce;
                std::string csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);

                response_status = 200;
                response_headers.push_back({"Content-Type", "application/json"});
                response_headers.push_back({"Content-Security-Policy", csp});
                for (const auto& [name, value] : security_headers) {
                    response_headers.push_back({name, value});
                }

                response_body = R"({"status":"alive"})";
                return true;
            });

        // Register assets endpoint (path-based)
        server.RegisterHandler("GET", "/assets",
            [&asset_resolver](std::string_view method,
                              std::string_view path,
                              const std::vector<std::pair<std::string, std::string>>& headers,
                              std::string_view body,
                              int& response_status,
                              std::vector<std::pair<std::string, std::string>>& response_headers,
                              std::string& response_body) -> bool {
                if (method != "GET") {
                    return false;
                }

                // Extract asset path (remove /assets prefix)
                std::string asset_path(path);
                if (asset_path.find("/assets") == 0) {
                    asset_path = asset_path.substr(7);  // Remove "/assets"
                }

                // Resolve asset path
                auto resolved = asset_resolver.ResolveSafePath(asset_path);
                if (!resolved.has_value()) {
                    // Return 404 with Problem Details
                    auto problem = ProblemDetails::NotFound(std::string(path));
                    response_status = 404;
                    response_headers.push_back(
                        {"Content-Type", std::string(ProblemDetails::ContentType())});
                    response_body = problem.ToJson();
                    return true;
                }

                // Read file and serve it
                std::ifstream file(resolved.value(), std::ios::binary);
                if (!file) {
                    auto problem = ProblemDetails::NotFound(std::string(path));
                    response_status = 404;
                    response_headers.push_back(
                        {"Content-Type", std::string(ProblemDetails::ContentType())});
                    response_body = problem.ToJson();
                    return true;
                }

                response_body =
                    std::string((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

                // Determine content type
                std::string content_type = "application/octet-stream";
                std::string ext = resolved.value().extension().string();
                if (ext == ".html") {
                    content_type = "text/html; charset=utf-8";
                } else if (ext == ".css") {
                    content_type = "text/css";
                } else if (ext == ".js") {
                    content_type = "application/javascript";
                } else if (ext == ".json") {
                    content_type = "application/json";
                } else if (ext == ".png") {
                    content_type = "image/png";
                } else if (ext == ".jpg" || ext == ".jpeg") {
                    content_type = "image/jpeg";
                } else if (ext == ".svg") {
                    content_type = "image/svg+xml";
                }

                response_status = 200;
                response_headers.push_back({"Content-Type", content_type});

                // Add security headers
                auto security_headers = SecurityHeaders::GetMandatorySecurityHeaders();
                std::string nonce;
                std::string csp = SecurityHeaders::GenerateCspHeaderNonceStrategy(nonce);
                response_headers.push_back({"Content-Security-Policy", csp});
                for (const auto& [name, value] : security_headers) {
                    response_headers.push_back({name, value});
                }

                return true;
            });

        // Register root endpoint
        server.RegisterHandler("GET", "/",
            [](std::string_view method,
               std::string_view path,
               const std::vector<std::pair<std::string, std::string>>& headers,
               std::string_view body,
               int& response_status,
               std::vector<std::pair<std::string, std::string>>& response_headers,
               std::string& response_body) -> bool {
                if (method != "GET") {
                    return false;
                }

                // Redirect to /assets/index.html
                response_status = 301;
                response_headers.push_back({"Location", "/assets/index.html"});
                return true;
            });

        // Start server
        std::cout << "Starting FHSS Dashboard HTTP server..." << std::endl;
        std::cout << "  Host: " << host << std::endl;
        std::cout << "  Port: " << port << std::endl;
        std::cout << "  Assets: " << asset_root << std::endl;

        if (!server.Start()) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }

        std::cout << "Server running. Press Ctrl+C to shutdown." << std::endl;

        // Keep server running
        while (server.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Server stopped." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
