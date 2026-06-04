// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelFormatting.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

class DeviceTransformNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          DeviceTransformNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    DeviceTransformNodeMetal() = default;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
        kernel_ = capability_bus.Get<capabilities::IMetalKernelCapability>();
        telemetry_ = capability_bus.Get<capabilities::IMetalTelemetryCapability>();
        return context_ != nullptr && memory_pool_ != nullptr && kernel_ != nullptr &&
               telemetry_ != nullptr;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!context_ || !memory_pool_ || !kernel_ || !telemetry_ || queue_id_ == 0 ||
            !accel::IsValidView(input) || !accel::IsValidKernelTicket(kernel_ticket_)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateDevice(input.bytes, input.device_id, lease)) {
            return std::nullopt;
        }

        auto output = lease.device_view;
        output.dtype = input.dtype;
        output.layout = input.layout;
        output.backend = input.backend;

        if (!accel::IsValidView(output)) {
            return std::nullopt;
        }

        void* const args[] = {output.device_ptr};
        if (!kernel_->Launch(kernel_ticket_, args, 1)) {
            return std::nullopt;
        }

        const auto* input_bytes = static_cast<const std::byte*>(input.device_ptr);
        auto* output_bytes = static_cast<std::byte*>(output.device_ptr);
        for (std::uint64_t index = 0; index < input.bytes; ++index) {
            const auto value = std::to_integer<std::uint8_t>(input_bytes[index]);
            output_bytes[index] = std::byte{static_cast<std::uint8_t>(value ^ kDeterministicXorMask)};
        }

        telemetry_->RecordKernel(kernel_ticket_, 0);
        last_output_lease_ = lease;
        last_kernel_ticket_ = kernel_ticket_;
        return output;
    }

    void ConfigureKernel(std::uint64_t kernel_id,
                         std::string_view kernel_name,
                         std::uint32_t device_id,
                         std::uint64_t queue_id) {
        kernel_ticket_.backend = accel::BackendKind::Metal;
        kernel_ticket_.kernel_id = kernel_id;
        kernel_ticket_.arg_count = 1;
        kernel_ticket_.execution_queue_id = queue_id;
        kernel_ticket_.launch.grid_x = 1;
        kernel_ticket_.launch.block_x = 1;
        queue_id_ = queue_id;
        device_id_ = device_id;
        if (kernel_) {
            kernel_->RegisterKernel(kernel_id, kernel_name);
        }
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalKernelCapability> kernel_;
    std::shared_ptr<capabilities::IMetalTelemetryCapability> telemetry_;
    std::uint64_t queue_id_{1};
    std::uint32_t device_id_{0};
    accel::KernelTicket kernel_ticket_{};
    accel::BufferLease last_output_lease_{};
    accel::KernelTicket last_kernel_ticket_{};

    static constexpr std::uint8_t kDeterministicXorMask = 0xA5;
};

} // namespace graph::gpu::metal::nodes