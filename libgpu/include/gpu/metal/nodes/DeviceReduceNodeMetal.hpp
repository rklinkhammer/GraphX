// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelFormatting.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

class DeviceReduceNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          DeviceReduceNodeMetal>,
    public graph::IGpuCapabilityBinding,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    DeviceReduceNodeMetal() = default;
    ~DeviceReduceNodeMetal() {
        if (owns_queue_ && context_ && queue_id_ != 0) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        shared_queue_ = capability_bus.Get<capabilities::IMetalSharedQueueCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
         transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        kernel_ = capability_bus.Get<capabilities::IMetalKernelCapability>();
        telemetry_ = capability_bus.Get<capabilities::IMetalTelemetryCapability>();
        if (queue_id_ == 0 && context_ != nullptr) {
            if (shared_queue_ != nullptr) {
                queue_id_ = shared_queue_->GetOrCreateQueueId();
                owns_queue_ = false;
            }
            if (queue_id_ == 0) {
                queue_id_ = context_->CreateCommandQueue();
                owns_queue_ = queue_id_ != 0;
            }
            if (kernel_ticket_.execution_queue_id == 0) {
                kernel_ticket_.execution_queue_id = queue_id_;
            }
        }
        if (kernel_ && has_pending_kernel_configuration_) {
            const auto effective_queue = configured_queue_id_ == 0 ? queue_id_ : configured_queue_id_;
            ConfigureKernel(configured_kernel_id_, configured_kernel_name_, configured_device_id_, effective_queue);
            has_pending_kernel_configuration_ = false;
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
        configured_kernel_id_ = kernel_id;
        configured_kernel_name_ = std::string(kernel_name);
        configured_device_id_ = device_id;
        configured_queue_id_ = queue_id;

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

    void Configure(const graph::JsonView& cfg) override {
        if (cfg.Contains("queue_id")) {
            auto parsed_queue = cfg.TryGetInt("queue_id");
            if (!parsed_queue) {
                throw parsed_queue.error();
            }
            if (parsed_queue.value() < 0) {
                throw std::invalid_argument("queue_id must be >= 0");
            }
            configured_queue_id_ = static_cast<std::uint64_t>(parsed_queue.value());
        }

        if (cfg.Contains("device_id")) {
            auto parsed_device = cfg.TryGetInt("device_id");
            if (!parsed_device) {
                throw parsed_device.error();
            }
            if (parsed_device.value() < 0) {
                throw std::invalid_argument("device_id must be >= 0");
            }
            configured_device_id_ = static_cast<std::uint32_t>(parsed_device.value());
        }

        if (cfg.Contains("kernel_id")) {
            auto parsed_kernel_id = cfg.TryGetInt("kernel_id");
            if (!parsed_kernel_id) {
                throw parsed_kernel_id.error();
            }
            if (parsed_kernel_id.value() <= 0) {
                throw std::invalid_argument("kernel_id must be > 0");
            }
            configured_kernel_id_ = static_cast<std::uint64_t>(parsed_kernel_id.value());
        }

        if (cfg.Contains("kernel_name")) {
            auto parsed_kernel_name = cfg.TryGetString("kernel_name");
            if (!parsed_kernel_name) {
                throw parsed_kernel_name.error();
            }
            if (parsed_kernel_name.value().empty()) {
                throw std::invalid_argument("kernel_name must be non-empty");
            }
            configured_kernel_name_ = parsed_kernel_name.value();
        }

        if (!configured_kernel_name_.empty() && configured_kernel_id_ != 0) {
            has_pending_kernel_configuration_ = true;
        }
    }

    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"queue_id", configured_queue_id_},
            {"device_id", configured_device_id_},
            {"kernel_id", configured_kernel_id_},
            {"kernel_name", configured_kernel_name_},
        };
        return graph::JsonView(params);
    }

    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override {
        static thread_local nlohmann::json desc;
        if (param_name == "queue_id") {
            desc = {{"type", "integer"}, {"required", false},
                    {"description", "Optional command queue id. 0 means node-owned queue."}};
        } else if (param_name == "device_id") {
            desc = {{"type", "integer"}, {"required", false},
                    {"description", "Target Metal device id for kernel execution."}};
        } else if (param_name == "kernel_id") {
            desc = {{"type", "integer"}, {"required", false},
                    {"description", "Kernel registration id for reduce operation."}};
        } else if (param_name == "kernel_name") {
            desc = {{"type", "string"}, {"required", false},
                    {"description", "Kernel function name for reduce operation."}};
        } else {
            desc = nlohmann::json::object();
        }
        return graph::JsonView(desc);
    }

    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"queue_id", "device_id", "kernel_id", "kernel_name"};
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
    std::shared_ptr<capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::shared_ptr<capabilities::IMetalKernelCapability> kernel_;
    std::shared_ptr<capabilities::IMetalTelemetryCapability> telemetry_;
    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    bool owns_queue_{false};
    accel::KernelTicket kernel_ticket_{};
    std::uint64_t configured_kernel_id_{0};
    std::string configured_kernel_name_{};
    std::uint32_t configured_device_id_{0};
    std::uint64_t configured_queue_id_{0};
    bool has_pending_kernel_configuration_{false};
    accel::BufferLease last_output_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
    accel::KernelTicket last_kernel_ticket_{};

    static constexpr std::uint64_t kMetricsElementCount = 2;
    static constexpr std::uint64_t kMetricsByteWidth = kMetricsElementCount * sizeof(std::uint32_t);
};

} // namespace graph::gpu::metal::nodes