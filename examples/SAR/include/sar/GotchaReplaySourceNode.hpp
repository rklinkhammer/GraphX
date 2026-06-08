#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "config/JsonView.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

struct GotchaNormalizedPulseRecord {
    std::uint64_t frame_id{};
    std::uint64_t pass_id{};
    std::uint64_t pulse_block_id{};
    std::uint64_t range_bin_start{};
    std::uint32_t range_bin_count{0};
    std::uint64_t aperture_span_start{};
    std::uint32_t aperture_span_count{1};
    std::uint64_t timestamp_us{};
    std::uint64_t ordering_key{};
    std::uint32_t stream_id{0};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
    std::array<float, 3> platform_position_m{};
    std::array<float, 3> platform_velocity_mps{};
    std::array<float, 3> scene_center_m{};
    float carrier_hz{0.0f};
    float bandwidth_hz{0.0f};
    float sample_rate_hz{0.0f};
    float calibration_gain{1.0f};
    float calibration_phase_rad{0.0f};
    std::string polarization{"unknown"};
    std::string coordinate_frame{"unknown"};
    std::string sample_layout{"interleaved_complex_f32"};
    std::string endianness{"native"};
    std::vector<SarIqSample> iq_samples{};
};

struct GotchaReplaySourceConfig {
    std::filesystem::path fixture_path{};
    bool emit_watermark{false};
    std::vector<GotchaNormalizedPulseRecord> records{};
};

class GotchaOfflineConverter {
public:
    std::vector<GotchaNormalizedPulseRecord> LoadFromFile(const std::filesystem::path& fixture_path) const;
    std::vector<GotchaNormalizedPulseRecord> LoadFromJson(const nlohmann::json& document) const;
};

// TODO(GOTCHA-INTEGRATION): replace fixture-based replay with a direct AFRL Gotcha reader
// once the raw file layout, calibration metadata, and redistribution constraints are confirmed.

class GotchaReplaySourceNode
    : public graph::NamedSourceNode<GotchaReplaySourceNode, SarPulseBlockMessage>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    GotchaReplaySourceNode() = default;
    explicit GotchaReplaySourceNode(GotchaReplaySourceConfig config);

    std::optional<SarPulseBlockMessage> Produce(
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 2> Fields() {
        return {{
            graph::JsonField{
                .name = "fixture_path",
                .type = graph::JsonType::String,
                .required = true,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "",
                .enum_values = std::nullopt,
                .description = "Path to a normalized Gotcha replay fixture"
            },
            graph::JsonField{
                .name = "emit_watermark",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Emit a watermark record before EOS"
            }
        }};
    }

    void Reset();
    void SetConfig(const GotchaReplaySourceConfig& config);
    const GotchaReplaySourceConfig& GetConfig() const noexcept;

private:
    SarPulseBlockMessage MakeMessage(const GotchaNormalizedPulseRecord& record) const;
    SarPulseBlockMessage MakeControlMessage(SarFrameMarker marker) const;

    GotchaOfflineConverter converter_{};
    GotchaReplaySourceConfig config_{};
    std::size_t next_record_index_{0};
    bool watermark_emitted_{false};
    bool eos_emitted_{false};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
