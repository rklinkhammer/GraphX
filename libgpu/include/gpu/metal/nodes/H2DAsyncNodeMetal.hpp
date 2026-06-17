/**
 * @file H2DAsyncNodeMetal.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class H2DAsyncNodeMetal
 * @brief H2DAsyncNodeMetal class.
 */
class H2DAsyncNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::HostPinnedBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          H2DAsyncNodeMetal>,
    public graph::IGpuCapabilityBinding,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    H2DAsyncNodeMetal() = default;
    ~H2DAsyncNodeMetal() {
        if (owns_queue_ && context_ && queue_id_ != 0) {
            context_->DestroyCommandQueue(queue_id_);
        }
    }

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override {
        context_ = capability_bus.Get<capabilities::IMetalContextCapability>();
        shared_queue_ = capability_bus.Get<capabilities::IMetalSharedQueueCapability>();
        memory_pool_ = capability_bus.Get<capabilities::IMetalMemoryPoolCapability>();
        transfer_ = capability_bus.Get<capabilities::IMetalTransferCapability>();
        if (queue_id_ == 0 && context_ != nullptr) {
            if (shared_queue_ != nullptr) {
                queue_id_ = shared_queue_->GetOrCreateQueueId();
                owns_queue_ = false;
            }
            if (queue_id_ == 0) {
                queue_id_ = context_->CreateCommandQueue();
                owns_queue_ = queue_id_ != 0;
            }
        }
        return memory_pool_ != nullptr && transfer_ != nullptr && queue_id_ != 0;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::HostPinnedBufferView& host_view,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || !transfer_ || queue_id_ == 0 ||
            !accel::IsValidView(host_view)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateDevice(host_view.bytes, device_id_, lease)) {
            return std::nullopt;
        }

        auto out_device_view = lease.device_view;
        out_device_view.dtype = host_view.dtype;
        out_device_view.layout = host_view.layout;

        if (!accel::IsValidView(out_device_view)) {
            return std::nullopt;
        }

        accel::TransferTicket ticket{};
        if (!transfer_->EnqueueH2D(host_view, out_device_view, queue_id_, ticket)) {
            return std::nullopt;
        }

        if (!accel::IsValidLease(lease) || !accel::IsValidTransferTicket(ticket)) {
            return std::nullopt;
        }

        last_device_lease_ = lease;
        last_transfer_ticket_ = ticket;
        return out_device_view;
    }

    void SetQueueAndDevice(std::uint64_t queue_id, std::uint32_t device_id) {
        owns_queue_ = false;
        queue_id_ = queue_id;
        device_id_ = device_id;
    }

    void Configure(const graph::JsonView& cfg) override {
        std::uint64_t queue_id = queue_id_;
        std::uint32_t device_id = device_id_;

        if (cfg.Contains("queue_id")) {
            auto parsed_queue = cfg.TryGetInt("queue_id");
            if (!parsed_queue) {
                throw parsed_queue.error();
            }
            if (parsed_queue.value() < 0) {
                throw std::invalid_argument("queue_id must be >= 0");
            }
            queue_id = static_cast<std::uint64_t>(parsed_queue.value());
        }

        if (cfg.Contains("device_id")) {
            auto parsed_device = cfg.TryGetInt("device_id");
            if (!parsed_device) {
                throw parsed_device.error();
            }
            if (parsed_device.value() < 0) {
                throw std::invalid_argument("device_id must be >= 0");
            }
            device_id = static_cast<std::uint32_t>(parsed_device.value());
        } else if (cfg.Contains("backend_id")) {
            auto parsed_backend = cfg.TryGetInt("backend_id");
            if (!parsed_backend) {
                throw parsed_backend.error();
            }
            if (parsed_backend.value() < 0) {
                throw std::invalid_argument("backend_id must be >= 0");
            }
            device_id = static_cast<std::uint32_t>(parsed_backend.value());
        }

        SetQueueAndDevice(queue_id, device_id);
    }

    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"queue_id", queue_id_},
            {"device_id", device_id_},
        };
        return graph::JsonView(params);
    }

    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override {
        static thread_local nlohmann::json desc;
        if (param_name == "queue_id") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Optional command queue id. 0 means node-owned queue."},
            };
        } else if (param_name == "device_id") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Target Metal device id for allocation and transfer."},
            };
        } else if (param_name == "backend_id") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Compatibility alias for device_id."},
            };
        } else if (param_name == "backend") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Compatibility field ignored by the concrete Metal node."},
            };
        } else if (param_name == "override_backend") {
            desc = {
                {"type", "boolean"},
                {"required", false},
                {"description", "Compatibility field ignored by the concrete Metal node."},
            };
        } else {
            desc = nlohmann::json::object();
        }
        return graph::JsonView(desc);
    }

    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"queue_id", "device_id", "backend_id", "backend", "override_backend"};
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    bool owns_queue_{false};
    accel::BufferLease last_device_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::metal::nodes
