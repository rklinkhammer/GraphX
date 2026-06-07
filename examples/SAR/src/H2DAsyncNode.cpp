#include "sar/H2DAsyncNode.hpp"

#include "config/ConfigError.hpp"

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

} // namespace

std::optional<SarRangeTileMessage> H2DAsyncNode::Transfer(
    const SarRangeTileMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    SarRangeTileMessage out = input;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    if (config_.override_backend) {
        out.envelope.backend_id = config_.backend_id;
        out.envelope.backend = config_.backend;
        out.buffer.device_index = config_.backend_id;
        out.buffer.backend = config_.backend;
    }
    return out;
}

void H2DAsyncNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("override_backend")) {
        auto value = cfg.TryGetBool("override_backend");
        if (!value) {
            throw value.error();
        }
        config.override_backend = value.value();
    }

    if (cfg.Contains("backend_id")) {
        auto value = cfg.TryGetInt("backend_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("backend_id must be >= 0");
        }
        config.backend_id = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("backend")) {
        auto value = cfg.TryGetInt("backend");
        if (!value) {
            throw value.error();
        }
        config.backend = ParseBackendKind(value.value());
    }

    SetConfig(config);
}

graph::JsonView H2DAsyncNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["override_backend"] = config_.override_backend;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView H2DAsyncNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const auto type = field.type;
            const char* type_name = "object";
            switch (type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> H2DAsyncNode::GetParameterNames() const {
    return {
        "override_backend",
        "backend_id",
        "backend",
    };
}

void H2DAsyncNode::SetConfig(const H2DAsyncConfig& config) {
    config_ = config;
}

const H2DAsyncConfig& H2DAsyncNode::GetConfig() const noexcept {
    return config_;
}

} // namespace sar
