/**
 * @file DspIqH2DNode.hpp
 * @brief DSP IQ host-to-device transfer node.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/IqPacket.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"
#include "graph/Message.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace dsp {

/**
 * @brief Token-preserving DSP IQ host-to-device transfer node.
 *
 * @details Packs `IqPacket<float, N>` from a `ControlToken<Message>` sidecar
 * into contiguous Float32 I/Q pairs and transfers that buffer to Metal device
 * memory. The sidecar remains authoritative for DSP identity; accelerator
 * views, leases, and tickets describe transport state only.
 */
template <std::size_t N = 256>
class DspIqH2DNode
    : public graph::NamedInteriorNode<
          graph::TypeList<graph::gpu::accel::ControlToken<graph::message::Message>>,
          graph::TypeList<graph::gpu::accel::ControlToken<graph::message::Message>>,
          DspIqH2DNode<N>>,
      public graph::IGpuCapabilityBinding,
      public graph::IConfigurable,
      public graph::IDiagnosable,
      public graph::IParameterized {
public:
    using IqPacketType = IqPacket<float, N>;
    using TokenType = graph::gpu::accel::ControlToken<graph::message::Message>;

    DspIqH2DNode();
    ~DspIqH2DNode() override;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override;

    std::optional<TokenType> Transfer(
        const TokenType& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    [[nodiscard]] graph::JsonView GetParameters() const override;
    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    [[nodiscard]] std::vector<std::string> GetParameterNames() const override;
    [[nodiscard]] graph::JsonView GetDiagnostics() const override;

    [[nodiscard]] const graph::gpu::accel::HostPinnedBufferView& last_host_view() const noexcept;
    [[nodiscard]] const graph::gpu::accel::DeviceBufferView& last_device_view() const noexcept;
    [[nodiscard]] const graph::gpu::accel::BufferLease& last_lease() const noexcept;
    [[nodiscard]] const graph::gpu::accel::TransferTicket& last_transfer_ticket() const noexcept;

private:
    void PackIqPacket(const IqPacketType& packet);
    [[nodiscard]] graph::gpu::accel::HostPinnedBufferView BuildHostView() const;
    [[nodiscard]] std::uint64_t ResolveQueueId();

    std::shared_ptr<graph::gpu::metal::capabilities::IMetalContextCapability> context_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalTransferCapability> transfer_;

    std::array<float, DspGpuBufferLayout<N>::kIqFloatCount> host_iq_buffer_{};
    graph::gpu::accel::HostPinnedBufferView last_host_view_{};
    graph::gpu::accel::DeviceBufferView last_device_view_{};
    graph::gpu::accel::BufferLease last_lease_{};
    graph::gpu::accel::TransferTicket last_transfer_ticket_{};

    mutable nlohmann::json parameters_cache_;
    mutable nlohmann::json parameter_description_cache_;
    mutable nlohmann::json diagnostics_cache_;

    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    bool owns_queue_{false};
};

extern template class DspIqH2DNode<256>;

}  // namespace dsp
