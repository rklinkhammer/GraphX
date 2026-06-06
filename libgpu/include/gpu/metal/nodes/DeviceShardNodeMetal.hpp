// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

class DeviceShardNodeMetal
    : public graph::NamedInteriorNode<
          graph::TypeList<accel::DeviceBufferView>,
          graph::TypeList<accel::DeviceBufferView>,
          DeviceShardNodeMetal>,
    public graph::IGpuCapabilityBinding,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    DeviceShardNodeMetal() = default;
    ~DeviceShardNodeMetal() {
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
        return context_ != nullptr && memory_pool_ != nullptr && transfer_ != nullptr && queue_id_ != 0;
    }

    std::optional<accel::DeviceBufferView> Transfer(
        const accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override {
        if (!context_ || !memory_pool_ || !transfer_ || !accel::IsValidView(input) ||
            shard_count_ == 0 || shard_index_ >= shard_count_) {
            return std::nullopt;
        }

        const auto shard_base_bytes = input.bytes / shard_count_;
        const auto shard_remainder_bytes = input.bytes % shard_count_;
        const auto shard_bytes = shard_base_bytes +
                                 ((shard_index_ == (shard_count_ - 1)) ? shard_remainder_bytes : 0U);
        if (shard_bytes == 0) {
            return std::nullopt;
        }

        accel::BufferLease lease{};
        if (!memory_pool_->AllocateShared(shard_bytes, input.device_id, lease)) {
            return std::nullopt;
        }

        accel::DeviceBufferView src_slice = input;
        src_slice.bytes = shard_bytes;
        const auto offset_bytes = shard_base_bytes * shard_index_;
        src_slice.device_ptr = static_cast<void*>(
            static_cast<std::byte*>(input.device_ptr) + static_cast<std::ptrdiff_t>(offset_bytes));

        auto output = lease.device_view;
        output.backend = input.backend;
        output.dtype = input.dtype;
        output.layout = input.layout;
        if (output.layout.rank > 0) {
            const auto base_dim0 = input.layout.shape[0] / shard_count_;
            const auto remainder_dim0 = input.layout.shape[0] % shard_count_;
            output.layout.shape[0] = base_dim0 +
                                     ((shard_index_ == (shard_count_ - 1)) ? remainder_dim0 : 0U);
            if (output.layout.shape[0] == 0) {
                output.layout.shape[0] = 1;
            }
        }
        output.device_id = input.device_id;
        output.execution_queue_id = input.execution_queue_id;

        const auto queue_id = input.execution_queue_id == 0 ? queue_id_ : input.execution_queue_id;
        if (queue_id == 0) {
            return std::nullopt;
        }
        if (!transfer_->EnqueueD2D(src_slice, output, queue_id, last_transfer_ticket_)) {
            return std::nullopt;
        }

        output.ready_event = last_transfer_ticket_.completion_event;
        last_shard_lease_ = lease;
        return output;
    }

    void ConfigureShard(std::uint32_t shard_index, std::uint32_t shard_count) {
        shard_index_ = shard_index;
        shard_count_ = shard_count;
    }

    void SetQueue(std::uint64_t queue_id) {
        owns_queue_ = false;
        queue_id_ = queue_id;
    }

    void Configure(const graph::JsonView& cfg) override {
        if (cfg.Contains("shard_index")) {
            auto shard_index = cfg.TryGetInt("shard_index");
            if (!shard_index) {
                throw shard_index.error();
            }
            if (shard_index.value() < 0) {
                throw std::invalid_argument("shard_index must be >= 0");
            }
            shard_index_ = static_cast<std::uint32_t>(shard_index.value());
        }

        if (cfg.Contains("shard_count")) {
            auto shard_count = cfg.TryGetInt("shard_count");
            if (!shard_count) {
                throw shard_count.error();
            }
            if (shard_count.value() <= 0) {
                throw std::invalid_argument("shard_count must be > 0");
            }
            shard_count_ = static_cast<std::uint32_t>(shard_count.value());
        }

        if (cfg.Contains("queue_id")) {
            auto queue_id = cfg.TryGetInt("queue_id");
            if (!queue_id) {
                throw queue_id.error();
            }
            if (queue_id.value() < 0) {
                throw std::invalid_argument("queue_id must be >= 0");
            }
            SetQueue(static_cast<std::uint64_t>(queue_id.value()));
        }
    }

    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"shard_index", shard_index_},
            {"shard_count", shard_count_},
            {"queue_id", queue_id_},
        };
        return graph::JsonView(params);
    }

    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override {
        static thread_local nlohmann::json desc;
        if (param_name == "shard_index") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Shard index to extract from input tensor."},
            };
        } else if (param_name == "shard_count") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Total number of shards used for partitioning."},
            };
        } else if (param_name == "queue_id") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Optional command queue id. 0 means node-owned queue."},
            };
        } else {
            desc = nlohmann::json::object();
        }
        return graph::JsonView(desc);
    }

    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"shard_index", "shard_count", "queue_id"};
    }

private:
    std::shared_ptr<capabilities::IMetalContextCapability> context_;
    std::shared_ptr<capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<capabilities::IMetalTransferCapability> transfer_;
    std::uint64_t queue_id_{0};
    bool owns_queue_{false};
    std::uint32_t shard_index_{0};
    std::uint32_t shard_count_{1};
    accel::BufferLease last_shard_lease_{};
    accel::TransferTicket last_transfer_ticket_{};
};

} // namespace graph::gpu::metal::nodes