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

class D2HAsyncNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::HostPinnedBufferView>,
          D2HAsyncNodeMetal>,
    public graph::IGpuCapabilityBinding,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    D2HAsyncNodeMetal() = default;
    ~D2HAsyncNodeMetal() {
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

    std::optional<accel::HostPinnedBufferView> Transfer(
        const accel::DeviceBufferView& device_view,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!memory_pool_ || !transfer_ || queue_id_ == 0 ||
            !accel::IsValidView(device_view)) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateHost(device_view.bytes, lease)) {
            return std::nullopt;
        }

        auto out_host_view = lease.host_view;
        out_host_view.dtype = device_view.dtype;
        out_host_view.layout = device_view.layout;

        if (!accel::IsValidView(out_host_view)) {
            return std::nullopt;
        }

        accel::TransferTicket ticket{};
        if (!transfer_->EnqueueD2H(device_view, out_host_view, queue_id_, ticket)) {
            return std::nullopt;
        }

        if (!accel::IsValidLease(lease) || !accel::IsValidTransferTicket(ticket)) {
            return std::nullopt;
        }

        last_host_lease_ = lease;
        last_transfer_ticket_ = ticket;
        return out_host_view;
    }

    void SetQueue(std::uint64_t queue_id) {
        owns_queue_ = false;
        queue_id_ = queue_id;
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
            SetQueue(static_cast<std::uint64_t>(parsed_queue.value()));
        }

        if (cfg.Contains("backend_id")) {
            auto parsed_backend = cfg.TryGetInt("backend_id");
            if (!parsed_backend) {
                throw parsed_backend.error();
            }
            if (parsed_backend.value() < 0) {
                throw std::invalid_argument("backend_id must be >= 0");
            }
        }
    }

    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"queue_id", queue_id_},
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
        } else if (param_name == "backend_id") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Compatibility field accepted by the concrete Metal node."},
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
        return {"queue_id", "backend_id", "backend", "override_backend"};
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint64_t queue_id_{0};
    bool owns_queue_{false};
    accel::BufferLease last_host_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::metal::nodes
