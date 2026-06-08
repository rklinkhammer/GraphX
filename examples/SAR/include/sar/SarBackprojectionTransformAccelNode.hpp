#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "gpu/metal/nodes/DeviceKernelNodeMetal.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

struct SarBackprojectionTransformAccelConfig {
    std::uint32_t image_width{16};
    std::uint32_t backend_id{0};
    std::uint64_t queue_id{0};
    std::uint64_t kernel_id{3301};
    SarBackendKind backend{SarBackendKind::SimulatedDevice};
};

class SarBackprojectionTransformAccelNode
    : public graph::NamedInteriorNode<
          graph::TypeList<graph::gpu::accel::DeviceBufferView>,
          graph::TypeList<graph::gpu::accel::DeviceBufferView>,
          SarBackprojectionTransformAccelNode>,
      public graph::IConfigurable,
      public graph::IParameterized,
      public graph::IGpuCapabilityBinding {
public:
    SarBackprojectionTransformAccelNode() = default;
    explicit SarBackprojectionTransformAccelNode(SarBackprojectionTransformAccelConfig config);

    std::optional<graph::gpu::accel::DeviceBufferView> Transfer(
        const graph::gpu::accel::DeviceBufferView& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{
                .name = "image_width",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "16",
                .enum_values = std::nullopt,
                .description = "Dispatch width hint for kernel launch metadata"
            },
            graph::JsonField{
                .name = "backend_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend device index"
            },
            graph::JsonField{
                .name = "queue_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Execution queue identifier. 0 selects backend_id + 1"
            },
            graph::JsonField{
                .name = "kernel_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "3301",
                .enum_values = std::nullopt,
                .description = "Kernel identifier"
            },
            graph::JsonField{
                .name = "backend",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = 2.0,
                .default_value = "1",
                .enum_values = std::nullopt,
                .description = "Backend kind enum: 0=Host, 1=SimulatedDevice, 2=NativeDevice"
            }
        }};
    }

    void SetConfig(const SarBackprojectionTransformAccelConfig& config);
    const SarBackprojectionTransformAccelConfig& GetConfig() const noexcept;

    const graph::gpu::accel::KernelTicket& last_kernel_ticket() const noexcept;
    bool native_kernel_bound() const noexcept;

private:
    void ConfigureNativeKernel();

    SarBackprojectionTransformAccelConfig config_{};
    std::uint64_t kernel_sequence_{0};
    graph::gpu::accel::KernelTicket last_kernel_ticket_{};
    graph::gpu::metal::nodes::DeviceKernelNodeMetal native_kernel_node_{};
    bool native_kernel_bound_{false};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
