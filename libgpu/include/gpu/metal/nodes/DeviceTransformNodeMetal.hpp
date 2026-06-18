/**
 * @file DeviceTransformNodeMetal.hpp
 * @brief Device Transform Node Metal GPU acceleration support.
 *
 * @details Provides Metal acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelFormatting.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/MetalKernelDescriptorParsing.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <algorithm>
#include <cstddef>
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

/**
 * @class DeviceTransformNodeMetal
 * @brief Device Transform Node Metal graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class DeviceTransformNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          DeviceTransformNodeMetal>,
    public graph::IGpuCapabilityBinding,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    /**
     * @brief Executes the Device Transform Node Metal operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    DeviceTransformNodeMetal() = default;
    /**
     * @brief Releases resources owned by Device Transform Node Metal.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    ~DeviceTransformNodeMetal() {
        if (owns_queue_ && context_ && queue_id_ != 0) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    /**
     * @brief Executes the Bind GPU Capabilities operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param capability_bus Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        shared_queue_ = capability_bus.Get<capabilities::IMetalSharedQueueCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
         transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        kernel_ = capability_bus.Get<capabilities::IMetalKernelCapability>();
        kernel_descriptor_capability_ = std::dynamic_pointer_cast<capabilities::IMetalKernelDescriptorCapability>(kernel_);
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
            if (has_typed_kernel_descriptor_) {
                /**
                 * @brief Applies configuration to this object.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param configured_kernel_descriptor_ Input or configuration value consumed by the method.
                 * @param configured_device_id_ Input or configuration value consumed by the method.
                 * @param effective_queue Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                ConfigureKernelDescriptor(configured_kernel_descriptor_, configured_device_id_, effective_queue);
            } else {
                /**
                 * @brief Applies configuration to this object.
                 *
                 * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
                 * @param configured_kernel_id_ Input or configuration value consumed by the method.
                 * @param configured_kernel_name_ Input or configuration value consumed by the method.
                 * @param configured_device_id_ Input or configuration value consumed by the method.
                 * @param effective_queue Input or configuration value consumed by the method.
                 * @return Method-specific result, status, or produced value when the signature provides one.
                 */
                ConfigureKernel(configured_kernel_id_, configured_kernel_name_, configured_device_id_, effective_queue);
            }
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

        if (!transfer_->EnqueueD2D(input, output, queue_id_, last_transfer_ticket_)) {
            return std::nullopt;
        }

        auto launch_ticket = kernel_ticket_;
        launch_ticket.arg_count = kDefaultArgCount;
        if (!PopulateRegisteredKernelExecution(launch_ticket)) {
            /**
             * @brief Executes the Apply Fallback Launch Defaults operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            ApplyFallbackLaunchDefaults(launch_ticket.launch);
        }
        launch_ticket.launch.grid_x = static_cast<std::uint32_t>(output.bytes);

        accel::DeviceBufferView* arg0 = &output;
        void* const args[] = {arg0};
        if (!kernel_->Launch(launch_ticket, args, 1)) {
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
        capabilities::MetalKernelDescriptor descriptor{};
        descriptor.kernel_id = kernel_id;
        descriptor.function_name = std::string(kernel_name);
        descriptor.source_kind = capabilities::MetalKernelSourceKind::Builtin;
        descriptor.arg_layout = {
            capabilities::MetalKernelArgDescriptor{capabilities::MetalKernelArgKind::DeviceBuffer,
                                                   capabilities::MetalKernelArgAccess::ReadWrite},
        };
        /**
         * @brief Applies configuration to this object.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @param descriptor Input or configuration value consumed by the method.
         * @param device_id Input or configuration value consumed by the method.
         * @param queue_id Input or configuration value consumed by the method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        ConfigureKernelDescriptor(descriptor, device_id, queue_id);
    }

    void ConfigureKernelDescriptor(const capabilities::MetalKernelDescriptor& descriptor,
                                   std::uint32_t device_id,
                                   std::uint64_t queue_id) {
        configured_kernel_descriptor_ = descriptor;
        has_typed_kernel_descriptor_ = true;
        configured_kernel_id_ = descriptor.kernel_id;
        configured_kernel_name_ = descriptor.function_name;
        configured_device_id_ = device_id;
        configured_queue_id_ = queue_id;

        kernel_ticket_.backend = accel::BackendKind::Metal;
        kernel_ticket_.kernel_id = descriptor.kernel_id;
        kernel_ticket_.arg_count = kDefaultArgCount;
        kernel_ticket_.execution_queue_id = queue_id;
        /**
         * @brief Executes the Apply Fallback Launch Defaults operation.
         *
         * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
         * @return Method-specific result, status, or produced value when the signature provides one.
         */
        ApplyFallbackLaunchDefaults(kernel_ticket_.launch);
        owns_queue_ = false;
        queue_id_ = queue_id;
        device_id_ = device_id;
        if (kernel_) {
            if (kernel_descriptor_capability_) {
                kernel_descriptor_capability_->RegisterKernelDescriptor(descriptor);
            } else {
                kernel_->RegisterKernel(descriptor.kernel_id, descriptor.function_name);
            }
            /**
             * @brief Updates or queries runtime registration through Populate Registered Kernel Execution.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param kernel_ticket_ Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            PopulateRegisteredKernelExecution(kernel_ticket_);
        }
    }

    /**
     * @brief Applies configuration to this object.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param cfg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Configure(const graph::JsonView& cfg) override {
        if (cfg.Contains("kernel_descriptor")) {
            auto descriptor_obj = cfg.TryGetObject("kernel_descriptor");
            if (!descriptor_obj) {
                throw descriptor_obj.error();
            }
            const auto descriptor = capabilities::ParseMetalKernelDescriptor(descriptor_obj.value());

            configured_kernel_descriptor_ = descriptor;
            configured_kernel_id_ = descriptor.kernel_id;
            configured_kernel_name_ = descriptor.function_name;
            has_typed_kernel_descriptor_ = true;
            has_pending_kernel_configuration_ = true;
        }

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

    /**
     * @brief Returns the Parameters.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"queue_id", configured_queue_id_},
            {"device_id", configured_device_id_},
            {"kernel_id", configured_kernel_id_},
            {"kernel_name", configured_kernel_name_},
            {"kernel_descriptor", {
                {"kernel_id", configured_kernel_descriptor_.kernel_id},
                {"function_name", configured_kernel_descriptor_.function_name},
                {"source_kind", configured_kernel_descriptor_.source_kind == capabilities::MetalKernelSourceKind::Builtin
                                    ? "builtin"
                                    : (configured_kernel_descriptor_.source_kind == capabilities::MetalKernelSourceKind::InlineSource
                                           ? "inline_source"
                                           : "metallib_path")},
            }},
        };
        return graph::JsonView(params);
    }

    /**
     * @brief Returns the Parameter Description.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param param_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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
                    {"description", "Kernel registration id for transform operation."}};
        } else if (param_name == "kernel_name") {
            desc = {{"type", "string"}, {"required", false},
                    {"description", "Kernel function name for transform operation."}};
        } else if (param_name == "kernel_descriptor") {
            desc = {{"type", "object"}, {"required", false},
                    {"description", "Typed kernel descriptor with source, dispatch and argument layout."}};
        } else {
            desc = nlohmann::json::object();
        }
        return graph::JsonView(desc);
    }

    /**
     * @brief Returns the Parameter Names.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"queue_id", "device_id", "kernel_id", "kernel_name", "kernel_descriptor"};
    }

private:
    static constexpr std::uint32_t kDefaultArgCount = 1;

    /**
     * @brief Executes the Apply Fallback Launch Defaults operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param launch Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    static void ApplyFallbackLaunchDefaults(accel::KernelLaunchConfig& launch) {
        launch.grid_x = 1;
        launch.grid_y = 1;
        launch.grid_z = 1;
        launch.block_x = 1;
        launch.block_y = 1;
        launch.block_z = 1;
    }

    /**
     * @brief Updates or queries runtime registration through Populate Registered Kernel Execution.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param ticket Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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
    std::shared_ptr<capabilities::IMetalKernelDescriptorCapability> kernel_descriptor_capability_;
    std::shared_ptr<capabilities::IMetalTelemetryCapability> telemetry_;
    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    bool owns_queue_{false};
    accel::KernelTicket kernel_ticket_{};
    std::uint64_t configured_kernel_id_{0};
    std::string configured_kernel_name_{};
    capabilities::MetalKernelDescriptor configured_kernel_descriptor_{};
    std::uint32_t configured_device_id_{0};
    std::uint64_t configured_queue_id_{0};
    bool has_typed_kernel_descriptor_{false};
    bool has_pending_kernel_configuration_{false};
    accel::BufferLease last_output_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
    accel::KernelTicket last_kernel_ticket_{};
};

} // namespace graph::gpu::metal::nodes
