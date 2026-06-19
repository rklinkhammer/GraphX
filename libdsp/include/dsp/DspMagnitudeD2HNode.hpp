/**
 * @file DspMagnitudeD2HNode.hpp
 * @brief DSP magnitude device-to-host transfer node.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

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
 * @brief Token-preserving DSP magnitude device-to-host transfer node.
 *
 * @details Consumes the device-backed magnitude token emitted by
 * `MetalSpectrumDftNode<N>`, copies contiguous Float32 magnitude bins back to
 * host memory, and reconstructs a host-side `MagnitudePacket<float, N>` with
 * peak-bin, peak-frequency, and peak-magnitude metadata.
 */
template <std::size_t N = 256>
class DspMagnitudeD2HNode
    : public graph::NamedInteriorNode<
          graph::TypeList<graph::gpu::accel::ControlToken<MagnitudePacket<float, N>>>,
          graph::TypeList<graph::gpu::accel::ControlToken<MagnitudePacket<float, N>>>,
          DspMagnitudeD2HNode<N>>,
      public graph::IGpuCapabilityBinding,
      public graph::IConfigurable,
      public graph::IDiagnosable,
      public graph::IParameterized {
public:
    using MagnitudePacketType = MagnitudePacket<float, N>;
    using TokenType = graph::gpu::accel::ControlToken<MagnitudePacketType>;

    DspMagnitudeD2HNode();
    ~DspMagnitudeD2HNode() override;

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
    [[nodiscard]] const graph::gpu::accel::TransferTicket& last_transfer_ticket() const noexcept;
    [[nodiscard]] const MagnitudePacketType& last_packet() const noexcept;

private:
    [[nodiscard]] graph::gpu::accel::HostPinnedBufferView BuildHostView();
    [[nodiscard]] std::uint64_t ResolveQueueId();
    [[nodiscard]] MagnitudePacketType ReconstructPacket(const MagnitudePacketType& input_sidecar) const;

    std::shared_ptr<graph::gpu::metal::capabilities::IMetalContextCapability> context_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalTransferCapability> transfer_;

    std::array<float, DspGpuBufferLayout<N>::kMagnitudeFloatCount> host_magnitude_buffer_{};
    graph::gpu::accel::HostPinnedBufferView last_host_view_{};
    graph::gpu::accel::DeviceBufferView last_device_view_{};
    graph::gpu::accel::TransferTicket last_transfer_ticket_{};
    MagnitudePacketType last_packet_{};

    mutable nlohmann::json parameters_cache_;
    mutable nlohmann::json parameter_description_cache_;
    mutable nlohmann::json diagnostics_cache_;

    std::uint64_t queue_id_{0};
    bool owns_queue_{false};
};

extern template class DspMagnitudeD2HNode<256>;

}  // namespace dsp
