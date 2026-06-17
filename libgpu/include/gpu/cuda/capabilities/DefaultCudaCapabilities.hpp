/**
 * @file DefaultCudaCapabilities.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace graph::gpu::cuda::capabilities {

/**
 * @class DefaultCudaContextCapability
 * @brief DefaultCudaContextCapability class.
 */
class DefaultCudaContextCapability final : public ICudaContextCapability {
public:
/**
 * @brief Set device.
 * @param device_id Parameter for set device.
 * @return Result of the operation.
 */
    bool SetDevice(std::uint32_t device_id) override;
/**
 * @brief Current device.
 * @return Result of the operation.
 */
    std::uint32_t CurrentDevice() const override;

/**
 * @brief Create stream.
 * @return Result of the operation.
 */
    std::uint64_t CreateStream() override;
/**
 * @brief Destroy stream.
 * @param stream_id Parameter for destroy stream.
 */
    void DestroyStream(std::uint64_t stream_id) override;

/**
 * @brief Create event.
 * @return Result of the operation.
 */
    std::uint64_t CreateEvent() override;
/**
 * @brief Destroy event.
 * @param event_id Parameter for destroy event.
 */
    void DestroyEvent(std::uint64_t event_id) override;

private:
    std::uint32_t current_device_id_{0};
    std::uint64_t next_stream_id_{1};
    std::uint64_t next_event_id_{1};
    std::unordered_set<std::uint64_t> streams_{};
    std::unordered_set<std::uint64_t> events_{};
};

/**
 * @class DefaultCudaMemoryPoolCapability
 * @brief DefaultCudaMemoryPoolCapability class.
 */
class DefaultCudaMemoryPoolCapability final : public ICudaMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                        accel::BufferLease& out_lease) override;
    bool AllocatePinnedHost(std::uint64_t bytes,
                            accel::BufferLease& out_lease) override;
/**
 * @brief Release.
 * @param lease Parameter for release.
 * @return Result of the operation.
 */
    bool Release(const accel::BufferLease& lease) override;

private:
    std::uint64_t next_allocation_id_{1};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_{};
};

/**
 * @class DefaultCudaTransferCapability
 * @brief DefaultCudaTransferCapability class.
 */
class DefaultCudaTransferCapability final : public ICudaTransferCapability {
public:
    bool EnqueueH2D(const accel::HostPinnedBufferView& src,
                    accel::DeviceBufferView& dst,
                    std::uint64_t stream_id,
                    accel::TransferTicket& out_ticket) override;

    bool EnqueueD2H(const accel::DeviceBufferView& src,
                    accel::HostPinnedBufferView& dst,
                    std::uint64_t stream_id,
                    accel::TransferTicket& out_ticket) override;

    bool EnqueueD2D(const accel::DeviceBufferView& src,
                    accel::DeviceBufferView& dst,
                    std::uint64_t stream_id,
                    accel::TransferTicket& out_ticket) override;

private:
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{1};
};

/**
 * @class DefaultCudaKernelCapability
 * @brief DefaultCudaKernelCapability class.
 */
class DefaultCudaKernelCapability final : public ICudaKernelCapability {
public:
    bool RegisterKernel(std::uint64_t kernel_id,
                        std::string_view kernel_name) override;

    bool Launch(const accel::KernelTicket& ticket,
                void* const* args,
                std::size_t arg_count) override;

private:
    std::unordered_set<std::uint64_t> registered_kernels_{};
};

/**
 * @class DefaultCudaTelemetryCapability
 * @brief DefaultCudaTelemetryCapability class.
 */
class DefaultCudaTelemetryCapability final : public ICudaTelemetryCapability {
public:
    void RecordTransfer(const accel::TransferTicket& ticket,
                        std::uint64_t duration_ns) override;
    void RecordKernel(const accel::KernelTicket& ticket,
                      std::uint64_t duration_ns) override;
/**
 * @brief Increment error counter.
 * @param error_code Parameter for increment error counter.
 */
    void IncrementErrorCounter(std::string_view error_code) override;

    [[nodiscard]] std::uint64_t TransferSamples() const { return transfer_samples_; }
    [[nodiscard]] std::uint64_t KernelSamples() const { return kernel_samples_; }
    [[nodiscard]] std::uint64_t ErrorCount() const { return error_count_; }

private:
    std::uint64_t transfer_samples_{0};
    std::uint64_t kernel_samples_{0};
    std::uint64_t error_count_{0};
};

/**
 * @class DefaultCudaCollectiveCapability
 * @brief DefaultCudaCollectiveCapability class.
 */
class DefaultCudaCollectiveCapability final : public ICudaCollectiveCapability {
public:
    bool AllReduce(accel::DeviceBufferView& in_out,
                   const accel::CollectiveTicket& ticket) override;
    bool AllGather(const accel::DeviceBufferView& input,
                   accel::DeviceBufferView& output,
                   const accel::CollectiveTicket& ticket) override;
    bool ReduceScatter(const accel::DeviceBufferView& input,
                       accel::DeviceBufferView& output,
                       const accel::CollectiveTicket& ticket) override;
};

} // namespace graph::gpu::cuda::capabilities
