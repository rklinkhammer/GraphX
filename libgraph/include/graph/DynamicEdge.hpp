// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>

#include "graph/EdgeFacade.hpp"
#include "graph/IPortFunction.hpp"
#include "graph/RuntimePort.hpp"

namespace graph {

[[nodiscard]] inline std::expected<void, RuntimePortConnectError>
ValidateDynamicEdgeCompatibility(
    const RuntimePortHandle& source,
    const RuntimePortHandle& destination,
    std::size_t capacity) {
    if (capacity == 0) {
        return std::unexpected(RuntimePortConnectError::CapacityInvalid);
    }

    if (source.descriptor.direction != PortDirection::Output ||
        destination.descriptor.direction != PortDirection::Input) {
        return std::unexpected(RuntimePortConnectError::DirectionMismatch);
    }

    if (source.descriptor.payload_type != destination.descriptor.payload_type) {
        return std::unexpected(RuntimePortConnectError::PayloadTypeMismatch);
    }

    if (!source.descriptor.transport_type.empty() &&
        !destination.descriptor.transport_type.empty() &&
        source.descriptor.transport_type != destination.descriptor.transport_type) {
        return std::unexpected(RuntimePortConnectError::TransportTypeMismatch);
    }

    return {};
}

class DynamicEdge final : public IEdgeBase {
public:
    DynamicEdge(RuntimePortHandle source,
                RuntimePortHandle destination,
                std::size_t capacity,
                std::shared_ptr<EdgeMetrics> metrics = nullptr)
        : source_(std::move(source)),
          destination_(std::move(destination)),
          capacity_(capacity),
          metrics_(std::move(metrics)) {}

    ~DynamicEdge() override {
        Stop();
        Join();
    }

    bool Init() override {
        auto compatible = ValidateDynamicEdgeCompatibility(source_, destination_, capacity_);
        if (!compatible) {
            return false;
        }

        if (source_.port && destination_.port) {
            auto connected = source_.port->ConnectTo(*destination_.port, capacity_);
            if (!connected) {
                return false;
            }
        }

        initialized_.store(true, std::memory_order_release);
        if (metrics_) {
            metrics_->initialized.store(true, std::memory_order_release);
        }
        return true;
    }

    bool Start() override {
        if (!initialized_.load(std::memory_order_acquire)) {
            return false;
        }

        running_.store(true, std::memory_order_release);

        if (source_.port && destination_.port && !transfer_thread_.joinable()) {
            transfer_thread_ = std::thread([this]() {
                thread_metrics_.thread_active.store(true, std::memory_order_release);
                thread_metrics_.active_thread_count.store(1, std::memory_order_release);
                while (running_.load(std::memory_order_acquire)) {
                    const auto transfer_start = std::chrono::high_resolution_clock::now();
                    auto moved = source_.port->TransferTo(*destination_.port);
                    const auto transfer_end = std::chrono::high_resolution_clock::now();
                    const auto transfer_ns = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            transfer_end - transfer_start)
                            .count());

                    thread_metrics_.total_iterations.fetch_add(1, std::memory_order_acq_rel);
                    if (!moved) {
                        thread_metrics_.total_idle_time_ns.fetch_add(transfer_ns, std::memory_order_acq_rel);
                        if (metrics_) {
                            metrics_->messages_rejected.fetch_add(1, std::memory_order_acq_rel);
                            metrics_->backpressure_events.fetch_add(1, std::memory_order_acq_rel);
                        }
                        running_.store(false, std::memory_order_release);
                        break;
                    }

                    if (moved.value()) {
                        thread_metrics_.transfer_calls.fetch_add(1, std::memory_order_acq_rel);
                        thread_metrics_.total_transfer_time_ns.fetch_add(transfer_ns, std::memory_order_acq_rel);
                        if (metrics_) {
                            metrics_->messages_enqueued.fetch_add(1, std::memory_order_acq_rel);
                            metrics_->messages_dequeued.fetch_add(1, std::memory_order_acq_rel);
                            metrics_->total_queue_time_ns.fetch_add(transfer_ns, std::memory_order_acq_rel);

                            const auto depth = destination_.port->GetQueueSize();
                            uint64_t peak = metrics_->peak_queue_depth.load(std::memory_order_acquire);
                            while (depth > peak &&
                                   !metrics_->peak_queue_depth.compare_exchange_weak(
                                       peak, depth, std::memory_order_release, std::memory_order_acquire)) {
                            }
                        }
                        continue;
                    }

                    const auto idle_start = std::chrono::high_resolution_clock::now();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    const auto idle_ns = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::high_resolution_clock::now() - idle_start)
                            .count());
                    thread_metrics_.total_idle_time_ns.fetch_add(idle_ns, std::memory_order_acq_rel);
                    thread_metrics_.total_queue_wait_ns.fetch_add(idle_ns, std::memory_order_acq_rel);

                    if (metrics_) {
                        const auto depth = destination_.port->GetQueueSize();
                        uint64_t peak = metrics_->peak_queue_depth.load(std::memory_order_acquire);
                        while (depth > peak &&
                               !metrics_->peak_queue_depth.compare_exchange_weak(
                                   peak, depth, std::memory_order_release, std::memory_order_acquire)) {
                        }
                    }
                }
                thread_metrics_.thread_active.store(false, std::memory_order_release);
                thread_metrics_.active_thread_count.store(0, std::memory_order_release);
            });
        }

        if (metrics_) {
            metrics_->started.store(true, std::memory_order_release);
        }
        return true;
    }

    void Stop() override {
        running_.store(false, std::memory_order_release);
    }

    void Join() override {
        if (transfer_thread_.joinable()) {
            transfer_thread_.join();
        }
    }

    bool JoinWithTimeout(std::chrono::milliseconds) override {
        Join();
        return true;
    }

    std::size_t GetSourceNodeId() const override {
        return source_.node_index;
    }

    std::size_t GetSourcePortId() const override {
        return source_.descriptor.id;
    }

    std::size_t GetDestNodeId() const override {
        return destination_.node_index;
    }

    std::size_t GetDestPortId() const override {
        return destination_.descriptor.id;
    }

    std::string GetMessageTypeName() const override {
        return source_.descriptor.payload_type;
    }

    std::string GetDescription() const override {
        std::ostringstream oss;
        oss << "DynamicEdge[" << source_.node_index << ":" << source_.descriptor.id
            << " -> " << destination_.node_index << ":" << destination_.descriptor.id
            << "] (" << source_.descriptor.payload_type << ")";
        return oss.str();
    }

    std::size_t GetQueueSize() const override {
        if (!metrics_) return 0;
        uint64_t enqueued = metrics_->messages_enqueued.load(std::memory_order_relaxed);
        uint64_t dequeued = metrics_->messages_dequeued.load(std::memory_order_relaxed);
        return enqueued > dequeued ? static_cast<std::size_t>(enqueued - dequeued) : 0u;
    }

    uint64_t GetMessagesEnqueued() const override {
        return metrics_ ? metrics_->messages_enqueued.load(std::memory_order_relaxed) : 0;
    }

    uint64_t GetMessagesDequeued() const override {
        return metrics_ ? metrics_->messages_dequeued.load(std::memory_order_relaxed) : 0;
    }

    uint64_t GetMessagesRejected() const override {
        return metrics_ ? metrics_->messages_rejected.load(std::memory_order_relaxed) : 0;
    }

    uint64_t GetBackpressureEvents() const override {
        return metrics_ ? metrics_->backpressure_events.load(std::memory_order_relaxed) : 0;
    }

    uint64_t GetPeakQueueDepth() const override {
        return metrics_ ? metrics_->peak_queue_depth.load(std::memory_order_relaxed) : 0;
    }

    const graph::ThreadMetrics& GetEdgeThreadMetrics() const override {
        return thread_metrics_;
    }

    bool IsInitialized() const override {
        return initialized_.load(std::memory_order_relaxed);
    }

    bool IsRunning() const override {
        return running_.load(std::memory_order_relaxed);
    }

private:
    RuntimePortHandle source_;
    RuntimePortHandle destination_;
    std::size_t capacity_;
    std::shared_ptr<EdgeMetrics> metrics_;
    graph::ThreadMetrics thread_metrics_{};
    std::thread transfer_thread_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
};

struct DynamicEdgeConfig {
    RuntimePortHandle source;
    RuntimePortHandle destination;
    std::size_t capacity;
};

}  // namespace graph