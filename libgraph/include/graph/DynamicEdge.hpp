/**
 * @file DynamicEdge.hpp
 * @brief Dynamic Edge Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <chrono>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
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

/**
 * @class DynamicEdge
 * @brief Dynamic Edge type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
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

    /**
     * @brief Releases resources owned by Dynamic Edge.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    ~DynamicEdge() override {
        /**
         * @brief Performs the Stop lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        Stop();
        /**
         * @brief Performs the Join lifecycle step.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        Join();
    }

    /**
     * @brief Performs the Init lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Init() override {
        auto compatible = ValidateDynamicEdgeCompatibility(source_, destination_, capacity_);
        if (!compatible) {
            return false;
        }

        // Descriptor-only fallback ports validate metadata but cannot transfer payloads.
        // For executable graphs, require queue-backed runtime ports.
        if (!source_.port || !destination_.port ||
            source_.descriptor.transport_type == "runtime.descriptor" ||
            destination_.descriptor.transport_type == "runtime.descriptor") {
            return false;
        }

        auto connected = source_.port->ConnectTo(*destination_.port, capacity_);
        if (!connected) {
            return false;
        }

        initialized_.store(true, std::memory_order_release);
        if (metrics_) {
            metrics_->initialized.store(true, std::memory_order_release);
        }
        return true;
    }

    /**
     * @brief Performs the Start lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool Start() override {
        if (!initialized_.load(std::memory_order_acquire)) {
            return false;
        }

        running_.store(true, std::memory_order_release);

        if (source_.port && destination_.port && !transfer_thread_.joinable()) {
            thread_exited_.store(false, std::memory_order_release);
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

                        // Treat transfer failures as transient backpressure unless explicitly fatal.
                        if (IsFatalTransferError(moved.error())) {
                            running_.store(false, std::memory_order_release);
                            break;
                        }

                        const auto idle_start = std::chrono::high_resolution_clock::now();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        const auto idle_ns = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::high_resolution_clock::now() - idle_start)
                                .count());
                        thread_metrics_.total_idle_time_ns.fetch_add(idle_ns, std::memory_order_acq_rel);
                        thread_metrics_.total_queue_wait_ns.fetch_add(idle_ns, std::memory_order_acq_rel);
                        continue;
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
                thread_exited_.store(true, std::memory_order_release);
                thread_exit_cv_.notify_all();
            });
        } else {
            thread_exited_.store(true, std::memory_order_release);
        }

        if (metrics_) {
            metrics_->started.store(true, std::memory_order_release);
        }
        return true;
    }

    /**
     * @brief Performs the Stop lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Stop() override {
        running_.store(false, std::memory_order_release);
    }

    /**
     * @brief Performs the Join lifecycle step.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Join() override {
        if (transfer_thread_.joinable()) {
            transfer_thread_.join();
        }
    }

    /**
     * @brief Executes the Join With Timeout operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param timeout_ms Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
        if (!transfer_thread_.joinable()) {
            return true;
        }

        /**
         * @brief Executes the Lock operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param thread_exit_mtx_ Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        std::unique_lock<std::mutex> lock(thread_exit_mtx_);
        if (!thread_exit_cv_.wait_for(lock, timeout_ms, [this] {
                return thread_exited_.load(std::memory_order_acquire);
            })) {
            return false;
        }
        lock.unlock();

        if (transfer_thread_.joinable()) {
            transfer_thread_.join();
        }
        return true;
    }

    /**
     * @brief Returns the Source Node ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetSourceNodeId() const override {
        return source_.node_index;
    }

    /**
     * @brief Returns the Source Port ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetSourcePortId() const override {
        return source_.descriptor.id;
    }

    /**
     * @brief Returns the Dest Node ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetDestNodeId() const override {
        return destination_.node_index;
    }

    /**
     * @brief Returns the Dest Port ID.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetDestPortId() const override {
        return destination_.descriptor.id;
    }

    /**
     * @brief Returns the Message Type Name.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::string GetMessageTypeName() const override {
        return source_.descriptor.payload_type;
    }

    /**
     * @brief Returns the Description.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::string GetDescription() const override {
        std::ostringstream oss;
        oss << "DynamicEdge[" << source_.node_index << ":" << source_.descriptor.id
            << " -> " << destination_.node_index << ":" << destination_.descriptor.id
            << "] (" << source_.descriptor.payload_type << ")";
        return oss.str();
    }

    /**
     * @brief Returns the Queue Size.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::size_t GetQueueSize() const override {
        return destination_.port ? destination_.port->GetQueueSize() : 0u;
    }

    /**
     * @brief Returns the Messages Enqueued.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetMessagesEnqueued() const override {
        return metrics_ ? metrics_->messages_enqueued.load(std::memory_order_relaxed) : 0;
    }

    /**
     * @brief Returns the Messages Dequeued.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetMessagesDequeued() const override {
        return metrics_ ? metrics_->messages_dequeued.load(std::memory_order_relaxed) : 0;
    }

    /**
     * @brief Returns the Messages Rejected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetMessagesRejected() const override {
        return metrics_ ? metrics_->messages_rejected.load(std::memory_order_relaxed) : 0;
    }

    /**
     * @brief Returns the Backpressure Events.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetBackpressureEvents() const override {
        return metrics_ ? metrics_->backpressure_events.load(std::memory_order_relaxed) : 0;
    }

    /**
     * @brief Returns the Peak Queue Depth.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    uint64_t GetPeakQueueDepth() const override {
        return metrics_ ? metrics_->peak_queue_depth.load(std::memory_order_relaxed) : 0;
    }

    /**
     * @brief Returns the Edge Thread Metrics.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    const graph::ThreadMetrics& GetEdgeThreadMetrics() const override {
        return thread_metrics_;
    }

    /**
     * @brief Reports whether Is Initialized is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool IsInitialized() const override {
        return initialized_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reports whether Is Running is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool IsRunning() const override {
        return running_.load(std::memory_order_relaxed);
    }

private:
    /**
     * @brief Reports whether Is Fatal Transfer Error is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param error Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    static bool IsFatalTransferError(RuntimePortConnectError error) {
        return error == RuntimePortConnectError::DirectionMismatch ||
               error == RuntimePortConnectError::PayloadTypeMismatch ||
               error == RuntimePortConnectError::NullDestination ||
               error == RuntimePortConnectError::TransportTypeMismatch ||
               error == RuntimePortConnectError::CapacityInvalid;
    }

    RuntimePortHandle source_;
    RuntimePortHandle destination_;
    std::size_t capacity_;
    std::shared_ptr<EdgeMetrics> metrics_;
    graph::ThreadMetrics thread_metrics_{};
    std::thread transfer_thread_;
    mutable std::mutex thread_exit_mtx_;
    std::condition_variable thread_exit_cv_;
    std::atomic<bool> thread_exited_{true};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
};

/**

 * @struct DynamicEdgeConfig

 * @brief Dynamic Edge Config data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct DynamicEdgeConfig {
    RuntimePortHandle source;
    RuntimePortHandle destination;
    std::size_t capacity;
};

}  // namespace graph
