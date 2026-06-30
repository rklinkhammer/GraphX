// SPDX-License-Identifier: MIT

/**
 * @file OrderedCrsdSetInputSourceNode.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarMessages.hpp"
#include "sar/io/CrsdReader.hpp"

#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sar {

struct OrderedCrsdSetInputSourceConfig {
    std::vector<std::string> crsd_paths{};
    std::string crsd_directory{};
    std::string manifest_path{};
    std::uint32_t stream_id{0};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

/**
 * @class OrderedCrsdSetInputSourceNode
 * @brief OrderedCrsdSetInputSourceNode class.
 */
class OrderedCrsdSetInputSourceNode
    : public graph::NamedSourceNode<OrderedCrsdSetInputSourceNode, SarControlToken>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    OrderedCrsdSetInputSourceNode();
    explicit OrderedCrsdSetInputSourceNode(
        OrderedCrsdSetInputSourceConfig config,
        graphx::sar::CrsdReaderPtr reader = std::make_shared<graphx::sar::CrsdReader>());

    std::optional<SarControlToken> Produce(std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 6> Fields() {
        return {{
            graph::JsonField{.name = "crsd_paths", .type = graph::JsonType::Array, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "[]", .enum_values = std::nullopt, .description = "Ordered list of CRSD product paths"},
            graph::JsonField{.name = "crsd_directory", .type = graph::JsonType::String, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "", .enum_values = std::nullopt, .description = "Directory containing product.crsd files"},
            graph::JsonField{.name = "manifest_path", .type = graph::JsonType::String, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "", .enum_values = std::nullopt, .description = "Manifest file declaring ordered product.crsd inputs"},
            graph::JsonField{.name = "stream_id", .type = graph::JsonType::Integer, .required = false, .min = 0.0, .max = std::nullopt, .default_value = "0", .enum_values = std::nullopt, .description = "SAR stream identifier"},
            graph::JsonField{.name = "backend_id", .type = graph::JsonType::Integer, .required = false, .min = 0.0, .max = std::nullopt, .default_value = "0", .enum_values = std::nullopt, .description = "Backend device index"},
            graph::JsonField{.name = "backend", .type = graph::JsonType::Integer, .required = false, .min = 0.0, .max = 2.0, .default_value = "0", .enum_values = std::nullopt, .description = "Backend kind enum: 0=Host, 1=SimulatedDevice, 2=NativeDevice"},
        }};
    }

    void Reset();
    void SetConfig(const OrderedCrsdSetInputSourceConfig& config);
    const OrderedCrsdSetInputSourceConfig& GetConfig() const noexcept;

    const graphx::sar::CrsdReadResult& GetLastReadResult() const noexcept;

private:
    SarControlToken MakeSegmentToken(const graphx::sar::CrsdSegmentRecord& segment) const;
    SarControlToken MakeEndOfStreamToken() const;

    OrderedCrsdSetInputSourceConfig config_{};
    graphx::sar::CrsdReaderPtr reader_{};
    graphx::sar::CrsdReadResult read_result_{};
    std::size_t next_segment_index_{0};
    bool eos_emitted_{false};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
