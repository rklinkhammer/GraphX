#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

namespace graphx::dsp::dashboard {

/**
 * @brief Loopback-only HTTP server for FHSS dashboard.
 *
 * Implements RFC 9110/9112 compliant HTTP/1.1 server with:
 * - Loopback-only binding (127.x.x.x for IPv4, ::1 for IPv6)
 * - Request/response size limits (DoS prevention)
 * - Graceful shutdown with signal handling
 * - Keep-Alive connection support
 * - Conditional request support (If-Match, If-Modified-Since)
 * - RFC 9457 Problem Details error format
 */
class DashboardHttpServer {
public:
    /**
     * @brief Server configuration options.
     */
    struct Options {
        /// Listen address (must be loopback: 127.x.x.x or ::1)
        std::string host = "127.0.0.1";
        
        /// Listen port
        uint16_t port = 8765;
        
        /// Max request header size (16 KiB)
        uint32_t max_header_bytes = 16 * 1024;
        
        /// Max request body size (64 MiB, configurable per-route)
        uint32_t max_body_bytes = 64 * 1024 * 1024;
        
        /// Max response body size (256 MiB)
        uint32_t max_response_bytes = 256 * 1024 * 1024;
        
        /// Request read timeout (seconds)
        uint32_t read_timeout_seconds = 30;
        
        /// Request write timeout (seconds)
        uint32_t write_timeout_seconds = 30;
        
        /// Idle connection timeout (seconds)
        uint32_t idle_timeout_seconds = 120;
        
        /// Total operation timeout (seconds)
        uint32_t operation_timeout_seconds = 300;
        
        /// Max concurrent connections
        uint32_t max_concurrent_connections = 8;
        
        /// Filesystem path to serve assets from
        std::string asset_root_path;
    };

    /**
     * @brief HTTP request/response handler signature.
     *
     * @param method HTTP method (GET, POST, etc.)
     * @param path Request path
     * @param headers Request headers (key-value pairs)
     * @param body Request body bytes
     * @param response_status Out: HTTP status code (e.g., 200, 404)
     * @param response_headers Out: Response headers (key-value pairs)
     * @param response_body Out: Response body bytes
     * @return true if handler succeeded, false if error should be generated
     */
    using RequestHandler = std::function<bool(
        std::string_view method,
        std::string_view path,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::string_view body,
        int& response_status,
        std::vector<std::pair<std::string, std::string>>& response_headers,
        std::string& response_body
    )>;

    /**
     * @brief Construct HTTP server with options.
     *
     * @param options Server configuration
     * @throws std::runtime_error if loopback binding validation fails
     */
    explicit DashboardHttpServer(const Options& options);

    /**
     * @brief Destructor. Ensures graceful shutdown.
     */
    ~DashboardHttpServer();

    /**
     * @brief Validate that host address is loopback-only.
     *
     * Accepts: 127.x.x.x for IPv4, ::1 for IPv6
     * Rejects: 0.0.0.0, ::/0, public addresses
     *
     * @param host Host address string
     * @return true if loopback, false otherwise
     * @throws std::runtime_error with descriptive error on violation
     */
    static bool ValidateLoopbackBinding(std::string_view host);

    /**
     * @brief Register an HTTP request handler.
     *
     * @param method HTTP method (GET, POST, etc.) - empty for all methods
     * @param path_prefix URL path prefix to match
     * @param handler Callback to handle matching requests
     */
    void RegisterHandler(
        std::string_view method,
        std::string_view path_prefix,
        RequestHandler handler
    );

    /**
     * @brief Start the HTTP server.
     *
     * Binds to configured address/port, starts acceptor thread.
     * Registers signal handlers for SIGTERM/SIGINT.
     *
     * @return true if server started successfully
     */
    bool Start();

    /**
     * @brief Stop the HTTP server.
     *
     * Marks server as not ready, drains existing connections,
     * joins executor threads with timeout.
     *
     * @return true if shutdown succeeded
     */
    bool Stop();

    /**
     * @brief Check if server is running.
     *
     * @return true if server is active and accepting connections
     */
    bool IsRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    /// Loopback-only validation flag
    void ValidateOptions();

    /// Signal handler for graceful shutdown
    static void SignalHandler(int signal);
};

}  // namespace graphx::dsp::dashboard
