// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "metrics/IMetricsSubscriber.hpp"

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
class GraphHttpServer : public app::metrics::IMetricsSubscriber {
public:
    struct MetricsCallbackObservation {
        std::size_t validations{0U};
        std::size_t target_key_constructions{0U};
        std::size_t samples_examined{0U};
        std::size_t samples_retained{0U};
        std::size_t mutex_acquisitions{0U};
        std::size_t socket_operations{0U};
        std::size_t http_responses{0U};
        std::size_t json_serializations{0U};
        std::size_t capability_reentries{0U};
    };

    GraphHttpServer(
        std::shared_ptr<GraphCoordinator> coordinator,
        std::shared_ptr<capabilities::CommandCapability> commands,
        std::shared_ptr<capabilities::MetricsCapability> metrics,
        int port = 8080, std::string index_path = {});
    ~GraphHttpServer() noexcept;

    bool Start();
    bool Stop();
    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] static constexpr std::size_t RequestWorkerLimit() {
        return 8U;
    }
    [[nodiscard]] static constexpr std::size_t PendingRequestLimit() {
        return 16U;
    }
    [[nodiscard]] std::size_t RetainedRequestWorkerCount() const;
    [[nodiscard]] std::size_t ActiveRequestCount() const;
    [[nodiscard]] std::size_t PendingRequestCount() const;
    [[nodiscard]] std::size_t RejectedRequestCount() const;
    /** Narrow deterministic synchronization hook for request-lifetime tests. */
    void SetMetricsSnapshotEntryHookForTesting(std::function<void()> hook);
    void SetMetricsBodyLimitForTesting(std::size_t bytes);
    /** Narrow observer used to prove callback work remains bounded and local. */
    void SetMetricsCallbackObserverForTesting(
        std::function<void(const MetricsCallbackObservation&)> observer);

    /** Bounded, non-blocking subscriber callback. */
    void OnMetricsEvent(const app::metrics::MetricsEvent& event) override;
    void OnMetricsGenerationReset(std::uint64_t generation) override;
    void OnMetricsSchemasChanged(
        std::uint64_t generation,
        const std::vector<app::metrics::NodeMetricsSchema>& schemas) override;

    GraphHttpServer(const GraphHttpServer&) = delete;
    GraphHttpServer& operator=(const GraphHttpServer&) = delete;
    GraphHttpServer(GraphHttpServer&&) = delete;
    GraphHttpServer& operator=(GraphHttpServer&&) = delete;

private:
    friend class GraphHttpServerMetricsCallbackProbe;
    [[nodiscard]] MetricsCallbackObservation
    ProbeMetricsCallbackBoundariesForTesting();
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace graph
