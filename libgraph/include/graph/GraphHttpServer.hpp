/**
 * @file GraphHttpServer.hpp
 * @brief REST API server and web UI for graph parameter viewing and editing.
 *
 * Provides HTTP-based access to graph parameters, execution control, and an
 * interactive web UI. Uses GraphCoordinator for parameter access and
 * GraphExecutor for lifecycle management.
 *
 * Key features:
 * - REST API endpoints for viewing/editing node parameters
 * - Web UI with node table, parameter editor, and execution controls
 * - Thread-safe HTTP server with configurable port
 * - Proper HTTP status codes and JSON response format
 * - Loopback-only binding for local operator access
 *
 * Usage:
 * @code
 * nlohmann::json graph = LoadGraphJson("graph.json");
 * auto executor = BuildExecutor();
 * auto server = std::make_unique<GraphHttpServer>(graph, executor.get());
 * if (server->Start()) {
 *   // Server running on port 8080
 *   // Visit http://localhost:8080
 *   server->Stop();
 * }
 * @endcode
 */

#pragma once

#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace graph {

// Forward declaration
class GraphExecutor;

/**
 * @brief HTTP server for graph parameter viewing, editing, and execution control.
 *
 * Provides a REST API and web UI for managing graph nodes and controlling
 * graph execution. All parameter edits are in-memory only (not persisted).
 *
 * Thread-safe. Non-copyable and non-movable.
 */
class GraphHttpServer {
public:
    /**
     * @brief Construct HTTP server for graph management.
     *
     * @param graph Reference to graph JSON object to manage.
     *              Must contain "nodes" array with node objects.
     * @param executor Pointer to GraphExecutor for lifecycle control.
     *                Can be nullptr (execution endpoints disabled).
     * @param port HTTP server port in the range 1-65535 (default 8080).
     * @param index_path Optional explicit path to the dashboard index resource.
     *
     * @pre graph must remain valid for server lifetime
     * @pre executor must remain valid for server lifetime (if provided)
     */
    explicit GraphHttpServer(nlohmann::json& graph,
                            GraphExecutor* executor = nullptr,
                            int port = 8080,
                            std::string index_path = {});

    /**
     * @brief Destructor. Calls Stop() if server is running.
     */
    ~GraphHttpServer() noexcept;

    /**
     * @brief Start the HTTP server.
     *
     * Binds to the configured port and begins accepting connections.
     * Runs in background thread.
     *
     * @return true if server started successfully, false if failed
     *         (port in use, bind failed, etc.)
     */
    bool Start();

    /**
     * @brief Stop the HTTP server.
     *
     * Closes all connections and shuts down the server thread.
     * Safe to call if server not running.
     *
     * @return true if server stopped successfully
     */
    bool Stop();

    /**
     * @brief Check if server is currently running.
     *
     * @return true if server is accepting connections
     */
    bool IsRunning() const;

    // Deleted copy/move semantics
    GraphHttpServer(const GraphHttpServer&) = delete;
    GraphHttpServer& operator=(const GraphHttpServer&) = delete;
    GraphHttpServer(GraphHttpServer&&) = delete;
    GraphHttpServer& operator=(GraphHttpServer&&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace graph
