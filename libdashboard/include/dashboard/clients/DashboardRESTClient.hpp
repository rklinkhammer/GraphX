// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace app::clients {

using json = nlohmann::json;

/**
 * @struct CommandResponse
 * @brief Response from a command execution
 */
struct CommandResponse {
    bool success = false;
    std::string status;
    std::string message;
    json data;
    uint64_t execution_time_ms = 0;
};

/**
 * @struct MetricsSnapshot
 * @brief Metrics snapshot from the server
 */
struct MetricsSnapshot {
    std::string timestamp;
    std::string source;
    std::string event_type;
    json data;
};

/**
 * @struct MetricsSchema
 * @brief Metrics schema definition
 */
struct MetricsSchema {
    std::string node_name;
    std::string node_type;
    json metrics_schema;
    std::vector<std::string> event_types;
};

/**
 * @class DashboardRESTClient
 * @brief HTTP REST client for connecting to WebUIAdapter
 *
 * Provides methods to interact with the Dashboard via REST API:
 * - Execute commands (pause, resume, stop, status, etc.)
 * - Query metrics schemas
 * - Get current metrics snapshot
 * - Handle errors and timeouts
 *
 * Thread-safe for multiple concurrent requests.
 *
 * Example usage:
 * ```cpp
 * DashboardRESTClient client("localhost", 8080);
 * auto result = client.ExecuteCommand("pause");
 * if (result.success) {
 *     std::cout << "Command executed: " << result.message << std::endl;
 * }
 * ```
 */
class DashboardRESTClient {
public:
    /**
     * @brief Construct REST client
     *
     * @param host Server hostname or IP (default: localhost)
     * @param port Server port (default: 8080)
     * @param timeout_ms Request timeout in milliseconds (default: 5000)
     */
    DashboardRESTClient(
        const std::string& host = "localhost",
        int port = 8080,
        int timeout_ms = 5000);

    /**
     * @brief Destructor
     */
    ~DashboardRESTClient() = default;

    /**
     * @brief Check if server is reachable
     *
     * @return true if server responds, false otherwise
     */
    bool IsConnected();

    /**
     * @brief Execute a dashboard command
     *
     * @param command Command name (e.g., "pause", "resume", "status")
     * @param args Optional command arguments
     * @return CommandResponse with result
     */
    CommandResponse ExecuteCommand(
        const std::string& command,
        const std::vector<std::string>& args = {});

    /**
     * @brief Get available metrics schemas
     *
     * @return Vector of MetricsSchema for all nodes
     * @throws std::runtime_error on connection error
     */
    std::vector<MetricsSchema> GetMetricsSchemas();

    /**
     * @brief Get current metrics snapshot
     *
     * @return MetricsSnapshot with latest data
     * @throws std::runtime_error on connection error
     */
    MetricsSnapshot GetLatestMetrics();

    /**
     * @brief Get server information
     *
     * @return JSON object with server details
     */
    json GetServerInfo();

    /**
     * @brief Set connection timeout
     *
     * @param timeout_ms Timeout in milliseconds
     */
    void SetTimeout(int timeout_ms) { timeout_ms_ = timeout_ms; }

    /**
     * @brief Set verbose logging
     *
     * @param verbose Enable verbose output
     */
    void SetVerbose(bool verbose) { verbose_ = verbose; }

    /**
     * @brief Get last error message
     *
     * @return Error message from last failed operation
     */
    const std::string& GetLastError() const { return last_error_; }

private:
    std::string host_;
    int port_;
    int timeout_ms_;
    bool verbose_;
    mutable std::string last_error_;

    /**
     * @brief Build URL for endpoint
     */
    std::string BuildUrl(const std::string& endpoint) const;

    /**
     * @brief Make HTTP GET request
     */
    json HttpGet(const std::string& endpoint);

    /**
     * @brief Make HTTP POST request
     */
    json HttpPost(const std::string& endpoint, const json& body = json::object());
};

}  // namespace app::clients
