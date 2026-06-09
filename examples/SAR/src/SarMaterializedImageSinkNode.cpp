#include "sar/SarMaterializedImageSinkNode.hpp"

#include "sar/SarAccelTokenImagePayloadStore.hpp"
#include "sar/SarMaterializedImageReference.hpp"

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

    auto payload = detail::ConsumeAccelTokenImagePayload(token);
    if (!payload) {
        return value;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_materialized_image_ = std::move(payload->pixels);
        last_capture_metadata_.sequence_id = payload->sequence_id;
        last_capture_metadata_.tile_id = payload->tile_id;
        last_capture_metadata_.element_count = last_materialized_image_.size();
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
    reference::BackprojectionAdapterConfig config{};
    return detail::BuildReferenceMaterializedImage(sequence_id, tile_id, element_count, config);
}

std::uint64_t SarMaterializedImageSinkNode::DecodeToken(const graph::gpu::accel::HostPinnedBufferView& value) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.host_ptr));
}

std::uint32_t SarMaterializedImageSinkNode::DecodeMarker(std::uint64_t token) {
    return static_cast<std::uint32_t>(token & 0x3u);
}

} // namespace sar