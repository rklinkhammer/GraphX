/**
 * @file MetalSpectrumDftNode.hpp
 * @brief Metal-backed DSP direct DFT spectrum node.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/IqPacket.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/Message.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace dsp {

/**
 * @brief Real Metal direct DFT spectrum transform for DSP IQ packets.
 *
 * @details This node consumes a device-backed `ControlToken<Message>` produced
 * by `DspIqH2DNode`, launches an inline Metal direct DFT kernel, and emits a
 * device-backed `ControlToken<MagnitudePacket<float, N>>`. It is intentionally
 * named DFT because it does not implement an optimized FFT.
 */
template <std::size_t N = 256>
class MetalSpectrumDftNode
    : public graph::NamedInteriorNode<
          graph::TypeList<graph::gpu::accel::ControlToken<graph::message::Message>>,
          graph::TypeList<graph::gpu::accel::ControlToken<MagnitudePacket<float, N>>>,
          MetalSpectrumDftNode<N>>,
      public graph::IGpuCapabilityBinding,
      public graph::IConfigurable,
      public graph::IDiagnosable,
      public graph::IParameterized {
public:
    using IqPacketType = IqPacket<float, N>;
    using InputTokenType = graph::gpu::accel::ControlToken<graph::message::Message>;
    using MagnitudePacketType = MagnitudePacket<float, N>;
    using OutputTokenType = graph::gpu::accel::ControlToken<MagnitudePacketType>;

    MetalSpectrumDftNode();
    ~MetalSpectrumDftNode() override;

    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override;

    std::optional<OutputTokenType> Transfer(
        const InputTokenType& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    [[nodiscard]] graph::JsonView GetParameters() const override;
    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    [[nodiscard]] std::vector<std::string> GetParameterNames() const override;
    [[nodiscard]] graph::JsonView GetDiagnostics() const override;

    [[nodiscard]] const graph::gpu::metal::capabilities::MetalKernelDescriptor&
    kernel_descriptor() const noexcept;
    [[nodiscard]] const graph::gpu::accel::KernelTicket& last_kernel_ticket() const noexcept;
    [[nodiscard]] const graph::gpu::accel::DeviceBufferView& last_input_view() const noexcept;
    [[nodiscard]] const graph::gpu::accel::DeviceBufferView& last_output_view() const noexcept;

private:
    [[nodiscard]] graph::gpu::metal::capabilities::MetalKernelDescriptor BuildKernelDescriptor() const;
    [[nodiscard]] std::string BuildKernelSource() const;
    [[nodiscard]] std::uint64_t ResolveQueueId();
    bool RegisterDftKernel();
    bool PopulateRegisteredKernelExecution(graph::gpu::accel::KernelTicket& ticket) const;

    std::shared_ptr<graph::gpu::metal::capabilities::IMetalContextCapability> context_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalSharedQueueCapability> shared_queue_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability> memory_pool_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalKernelCapability> kernel_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalKernelDescriptorCapability> kernel_descriptor_capability_;
    std::shared_ptr<graph::gpu::metal::capabilities::IMetalTelemetryCapability> telemetry_;

    graph::gpu::metal::capabilities::MetalKernelDescriptor kernel_descriptor_{};
    graph::gpu::accel::BufferLease last_output_lease_{};
    graph::gpu::accel::DeviceBufferView last_input_view_{};
    graph::gpu::accel::DeviceBufferView last_output_view_{};
    graph::gpu::accel::KernelTicket last_kernel_ticket_{};

    mutable nlohmann::json parameters_cache_;
    mutable nlohmann::json parameter_description_cache_;
    mutable nlohmann::json diagnostics_cache_;

    std::uint64_t queue_id_{0};
    std::uint32_t device_id_{0};
    std::uint64_t kernel_id_{0x4453504446543235ULL};
    bool owns_queue_{false};
    bool kernel_registered_{false};
    std::uint64_t kernel_sequence_{0};
};

extern template class MetalSpectrumDftNode<256>;

}  // namespace dsp
