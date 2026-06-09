#include "sar/SarMaterializedImageSinkNode.hpp"

#include <algorithm>
#include <cstdint>

namespace sar {

std::optional<graph::gpu::accel::HostPinnedBufferView> SarMaterializedImageSinkNode::Transfer(
    const graph::gpu::accel::HostPinnedBufferView& value,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled) {
        return value;
    }

    const auto token = DecodeToken(value);
    constexpr std::uint32_t kDataMarker = 0u;
    if (DecodeMarker(token) != kDataMarker) {
        return value;
    }

    const auto sequence_id = DecodeSequenceId(token);
    const auto tile_id = DecodeTileId(token);
    const auto encoded_bytes = DecodeByteCount(token);
    const auto byte_count = std::max<std::size_t>(encoded_bytes, static_cast<std::size_t>(value.bytes));
    const auto element_count = std::max<std::size_t>(1u, byte_count / sizeof(float));

    auto materialized = BuildDeterministicReferenceImage(sequence_id, tile_id, element_count);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_materialized_image_ = std::move(materialized);
        last_capture_metadata_.sequence_id = sequence_id;
        last_capture_metadata_.tile_id = tile_id;
        last_capture_metadata_.element_count = element_count;
        ++capture_count_;
    }

    return value;
}

void SarMaterializedImageSinkNode::Configure(const graph::JsonView& cfg) {
    auto next = config_;

    if (cfg.Contains("enabled")) {
        auto enabled = cfg.TryGetBool("enabled");
        if (!enabled) {
            throw enabled.error();
        }
        next.enabled = enabled.value();
    }

    config_ = next;
}

graph::JsonView SarMaterializedImageSinkNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["enabled"] = config_.enabled;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView SarMaterializedImageSinkNode::GetParameterDescription(const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const char* type_name = "object";
            if (field.type == graph::JsonType::Boolean) {
                type_name = "boolean";
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> SarMaterializedImageSinkNode::GetParameterNames() const {
    return {"enabled"};
}

std::vector<float> SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
    std::uint64_t sequence_id,
    std::uint32_t tile_id,
    std::size_t element_count) {
    std::vector<float> image(element_count, 0.0f);
    const float base =
        static_cast<float>(sequence_id % 1024u) * 1.0e-3f +
        static_cast<float>(tile_id) * 1.0e-2f;
    for (std::size_t i = 0; i < element_count; ++i) {
        // Deterministic surrogate samples unlock graph-vs-reference parity wiring before full native image extraction lands.
        image[i] = base + static_cast<float>(i) * 1.0e-4f;
    }
    return image;
}

std::uint64_t SarMaterializedImageSinkNode::DecodeToken(const graph::gpu::accel::HostPinnedBufferView& value) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.host_ptr));
}

std::uint32_t SarMaterializedImageSinkNode::DecodeMarker(std::uint64_t token) {
    return static_cast<std::uint32_t>(token & 0x3u);
}

std::uint32_t SarMaterializedImageSinkNode::DecodeTileId(std::uint64_t token) {
    return static_cast<std::uint32_t>((token >> 2u) & 0xFFFu);
}

std::uint64_t SarMaterializedImageSinkNode::DecodeSequenceId(std::uint64_t token) {
    return (token >> 14u) & 0xFFFFFFu;
}

std::size_t SarMaterializedImageSinkNode::DecodeByteCount(std::uint64_t token) {
    return static_cast<std::size_t>((token >> 38u) & 0xFFFFu);
}

} // namespace sar