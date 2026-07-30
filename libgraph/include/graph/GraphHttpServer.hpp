// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>

namespace capabilities {
class CommandCapability;
class MetricsCapability;
}

namespace graph {

class GraphCoordinator;

/**
 * Loopback HTTP adapter for the authoritative coordinator and typed executor
 * capabilities.  It does not own or directly invoke a GraphExecutor.
 */
class GraphHttpServer {
public:
    GraphHttpServer(
        std::shared_ptr<GraphCoordinator> coordinator,
        std::shared_ptr<capabilities::CommandCapability> commands,
        std::shared_ptr<capabilities::MetricsCapability> metrics,
        int port = 8080, std::string index_path = {});
    ~GraphHttpServer() noexcept;

    bool Start();
    bool Stop();
    [[nodiscard]] bool IsRunning() const;

    GraphHttpServer(const GraphHttpServer&) = delete;
    GraphHttpServer& operator=(const GraphHttpServer&) = delete;
    GraphHttpServer(GraphHttpServer&&) = delete;
    GraphHttpServer& operator=(GraphHttpServer&&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace graph
