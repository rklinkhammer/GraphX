// SPDX-License-Identifier: MIT

/**
 * @file D2HAsyncAccelNode.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
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

struct D2HAsyncAccelConfig {
    bool override_backend{false};
    std::uint32_t backend_id{0};
    std::uint64_t queue_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

/**
 * @class D2HAsyncAccelNode
 * @brief D2HAsyncAccelNode class.
 */
class D2HAsyncAccelNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarControlToken>,
          graph::TypeList<SarControlToken>,
          D2HAsyncAccelNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    D2HAsyncAccelNode() = default;

    std::optional<SarControlToken> Transfer(
        const SarControlToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 4> Fields() {
        return {{
            graph::JsonField{
                .name = "override_backend",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Override backend metadata on outgoing views"
            },
            graph::JsonField{
                .name = "backend_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend device index for override mode"
            },
            graph::JsonField{
                .name = "queue_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Execution queue id. 0 selects backend_id + 1"
            },
            graph::JsonField{
                .name = "backend",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = 2.0,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend kind enum for override mode"
            }
        }};
    }

    void SetConfig(const D2HAsyncAccelConfig& config);
    const D2HAsyncAccelConfig& GetConfig() const noexcept;

    const graph::gpu::accel::TransferTicket& last_transfer_ticket() const noexcept;
    const graph::gpu::accel::BufferLease& last_lease() const noexcept;

private:
    D2HAsyncAccelConfig config_{};
    std::uint64_t transfer_sequence_{0};
    graph::gpu::accel::TransferTicket last_transfer_ticket_{};
    graph::gpu::accel::BufferLease last_lease_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
