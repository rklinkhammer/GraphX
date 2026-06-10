#include "sar/SarMaterializedImageSinkNode.hpp"

#include "sar/SarMaterializedImageReference.hpp"

#include <algorithm>

namespace sar {

std::optional<SarAccelControlToken> SarMaterializedImageSinkNode::Transfer(
    const SarAccelControlToken& value,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled) {
        return value;
    }

    if (value.sidecar.marker != SarFrameMarker::Data) {
        return value;
    }

    if (!value.has_kernel_ticket) {
        return value;
    }

    if (value.sidecar.sequence_id == 0u && value.sidecar.tile_id == 0u) {
        return value;
    }

    const auto element_count = std::max<std::size_t>(
        1u,
        static_cast<std::size_t>((value.sidecar.payload_byte_count > 0u)
                                     ? (value.sidecar.payload_byte_count / sizeof(float))
                                     : (value.has_host_view ? value.host_view.bytes / sizeof(float) : 1u)));
    auto image = BuildDeterministicReferenceImage(
        value.sidecar.sequence_id,
        value.sidecar.tile_id,
        element_count);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_materialized_image_ = std::move(image);
        last_capture_metadata_.sequence_id = value.sidecar.sequence_id;
        last_capture_metadata_.tile_id = value.sidecar.tile_id;
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

} // namespace sar