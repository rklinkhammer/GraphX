// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelFormatting.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

class DeviceReduceNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          DeviceReduceNodeMetal>,
      public graph::IGpuCapabilityBinding {
public:
    DeviceReduceNodeMetal() = default;
    ~DeviceReduceNodeMetal() {
        if (owns_queue_ && context_ && queue_id_ != 0) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
         transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        kernel_ = capability_bus.Get<capabilities::IMetalKernelCapability>();
        telemetry_ = capability_bus.Get<capabilities::IMetalTelemetryCapability>();
        if (queue_id_ == 0 && context_ != nullptr) {
            queue_id_ = context_->CreateCommandQueue();
            owns_queue_ = queue_id_ != 0;
            if (kernel_ticket_.execution_queue_id == 0) {
                kernel_ticket_.execution_queue_id = queue_id_;
            }
        }
         return context_ != nullptr && memory_pool_ != nullptr && transfer_ != nullptr &&
             kernel_ != nullptr && queue_id_ != 0 &&
               telemetry_ != nullptr;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!context_ || !memory_pool_ || !transfer_ || !kernel_ || !telemetry_ || queue_id_ == 0 ||
            !accel::IsValidView(input) || !accel::IsValidKernelTicket(kernel_ticket_)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateDevice(kMetricsByteWidth, input.device_id, lease)) {
            return std::nullopt;
        }

        auto output = lease.device_view;
        output.dtype = accel::DataType::UInt32;
        output.layout.rank = 1;
        output.layout.shape[0] = kMetricsElementCount;
        output.layout.stride[0] = 1;
        output.backend = input.backend;

        if (!accel::IsValidView(output)) {
            return std::nullopt;
        }

        auto launch_ticket = kernel_ticket_;
        launch_ticket.arg_count = kDefaultArgCount;
        if (!PopulateRegisteredKernelExecution(launch_ticket)) {
            ApplyFallbackLaunchDefaults(launch_ticket.launch);
        }
        launch_ticket.launch.grid_x = static_cast<std::uint32_t>(input.bytes);

        accel::DeviceBufferView* arg0 = const_cast<accel::DeviceBufferView*>(&input);
        accel::DeviceBufferView* arg1 = &output;
        void* const args[] = {arg0, arg1};
        if (!kernel_->Launch(launch_ticket, args, 2)) {
            return std::nullopt;
        }

        telemetry_->RecordKernel(launch_ticket, 0);
        last_output_lease_ = lease;
        last_kernel_ticket_ = launch_ticket;
        return output;
    }

    void ConfigureKernel(std::uint64_t kernel_id,
                         std::string_view kernel_name,
                         std::uint32_t device_id,
                         std::uint64_t queue_id) {
        kernel_ticket_.backend = accel::BackendKind::Metal;
        kernel_ticket_.kernel_id = kernel_id;
        kernel_ticket_.arg_count = kDefaultArgCount;
        kernel_ticket_.execution_queue_id = queue_id;
        ApplyFallbackLaunchDefaults(kernel_ticket_.launch);
        owns_queue_ = false;
        queue_id_ = queue_id;
        device_id_ = device_id;
        if (kernel_) {
            kernel_->RegisterKernel(kernel_id, kernel_name);
            PopulateRegisteredKernelExecution(kernel_ticket_);
        }
    }

private:
    static constexpr std::uint32_t kDefaultArgCount = 2;

    static void ApplyFallbackLaunchDefaults(accel::KernelLaunchConfig& launch) {
        launch.grid_x = 1;
        launch.grid_y = 1;
        launch.grid_z = 1;
        launch.block_x = 1;
        launch.block_y = 1;
        launch.block_z = 1;
    }

    bool PopulateRegisteredKernelExecution(accel::KernelTicket& ticket) const {
        if (!kernel_) {
            return false;
        }

        capabilities::IMetalKernelCapability::RegisteredKernelExecution execution{};
        if (!kernel_->TryGetRegisteredKernelExecution(ticket.kernel_id, execution)) {
            return false;
        }

        ticket.launch = execution.dispatch;
        if (execution.arg_count != 0) {
            ticket.arg_count = execution.arg_count;
        }
        return true;
    }

    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::shared_ptr<capabilities::IMetalKernelCapability> kernel_;
    std::shared_ptr<capabilities::IMetalTelemetryCapability> telemetry_;
    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    bool owns_queue_{false};
    accel::KernelTicket kernel_ticket_{};
    accel::BufferLease last_output_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
    accel::KernelTicket last_kernel_ticket_{};

    static constexpr std::uint64_t kMetricsElementCount = 2;
    static constexpr std::uint64_t kMetricsByteWidth = kMetricsElementCount * sizeof(std::uint32_t);
};

} // namespace graph::gpu::metal::nodes