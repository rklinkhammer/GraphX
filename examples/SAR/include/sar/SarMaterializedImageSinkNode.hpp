#pragma once

#include "config/Config.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

struct SarMaterializedImageSinkConfig {
    bool enabled{false};
};

struct SarMaterializedCaptureMetadata {
    std::uint64_t sequence_id{0};
    std::uint32_t tile_id{0};
    std::size_t element_count{0};
};

class SarMaterializedImageSinkNode
    : public graph::NamedInteriorNode<
          graph::TypeList<graph::gpu::accel::HostPinnedBufferView>,
          graph::TypeList<graph::gpu::accel::HostPinnedBufferView>,
          SarMaterializedImageSinkNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    SarMaterializedImageSinkNode() = default;

    std::optional<graph::gpu::accel::HostPinnedBufferView> Transfer(
        const graph::gpu::accel::HostPinnedBufferView& value,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 1> Fields() {
        return {{
            graph::JsonField{
                .name = "enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Enable deterministic in-memory image materialization"
            }
        }};
    }

    const SarMaterializedImageSinkConfig& GetConfig() const noexcept {
        return config_;
    }

    std::size_t capture_count() const noexcept {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return capture_count_;
    }

    bool has_materialized_image() const noexcept {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return !last_materialized_image_.empty();
    }

    std::vector<float> last_materialized_image() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_materialized_image_;
    }

    SarMaterializedCaptureMetadata last_capture_metadata() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_capture_metadata_;
    }

    static std::vector<float> BuildDeterministicReferenceImage(
        std::uint64_t sequence_id,
        std::uint32_t tile_id,
        std::size_t element_count);

private:
    static std::uint64_t DecodeToken(const graph::gpu::accel::HostPinnedBufferView& value);
    static std::uint32_t DecodeMarker(std::uint64_t token);

    SarMaterializedImageSinkConfig config_{};

    mutable std::mutex state_mutex_;
    std::size_t capture_count_{0};
    std::vector<float> last_materialized_image_{};
    SarMaterializedCaptureMetadata last_capture_metadata_{};

    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar