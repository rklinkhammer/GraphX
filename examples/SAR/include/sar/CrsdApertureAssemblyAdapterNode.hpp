#pragma once

#include "sar/SarMessages.hpp"
#include "sar/SarPhaseHistoryModel.hpp"
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace sar {

struct CrsdApertureAssemblyAdapterConfig {
    std::vector<std::string> crsd_paths{};
    std::string crsd_directory{};
    std::string manifest_path{};
    bool require_contiguous_segment_indices{true};
    bool enable_sidecar_pulse_range_cross_check{false};
};

class CrsdApertureAssemblyAdapterNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarAccelControlToken>,
          graph::TypeList<SarPhaseHistoryControlMessage>,
          CrsdApertureAssemblyAdapterNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    CrsdApertureAssemblyAdapterNode();
    explicit CrsdApertureAssemblyAdapterNode(
        CrsdApertureAssemblyAdapterConfig config,
        graphx::sar::CrsdReaderPtr reader = std::make_shared<graphx::sar::CrsdReader>());

    std::optional<SarPhaseHistoryControlMessage> Transfer(
        const SarAccelControlToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{.name = "crsd_paths", .type = graph::JsonType::Array, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "[]", .enum_values = std::nullopt, .description = "Ordered list of CRSD product paths"},
            graph::JsonField{.name = "crsd_directory", .type = graph::JsonType::String, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "", .enum_values = std::nullopt, .description = "Directory containing product.crsd files"},
            graph::JsonField{.name = "manifest_path", .type = graph::JsonType::String, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "", .enum_values = std::nullopt, .description = "Manifest declaring ordered CRSD products"},
            graph::JsonField{.name = "require_contiguous_segment_indices", .type = graph::JsonType::Boolean, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "true", .enum_values = std::nullopt, .description = "Require contiguous segment ordering"},
            graph::JsonField{.name = "enable_sidecar_pulse_range_cross_check", .type = graph::JsonType::Boolean, .required = false, .min = std::nullopt, .max = std::nullopt, .default_value = "false", .enum_values = std::nullopt, .description = "Optional sidecar pulse range cross-check"},
        }};
    }

    void Reset();
    const CrsdApertureAssemblyAdapterConfig& GetConfig() const noexcept;
    const std::string& GetLastDiagnostic() const noexcept;
    const std::optional<SarPhaseHistoryApertureFrame>& GetLastFrame() const noexcept;

private:
    void BuildExpectedSegmentIndexMap();
    void ValidateAssemblyConsistency() const;
    std::optional<std::string> ValidateDataToken(const SarAccelControlToken& input);
    std::optional<SarPhaseHistoryControlMessage> BuildApertureMessage(const SarAccelControlToken& eos_token);

    CrsdApertureAssemblyAdapterConfig config_{};
    graphx::sar::CrsdReaderPtr reader_{};
    graphx::sar::CrsdReadResult read_result_{};

    std::unordered_map<std::uint64_t, const graphx::sar::CrsdSegmentRecord*> segments_by_index_{};
    std::unordered_set<std::uint64_t> seen_segment_indices_{};
    std::vector<std::uint64_t> received_segment_order_{};
    std::uint64_t expected_next_segment_index_{0};
    bool completion_emitted_{false};

    std::string last_diagnostic_{"unconfigured"};
    std::optional<SarPhaseHistoryApertureFrame> last_frame_{std::nullopt};

    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
