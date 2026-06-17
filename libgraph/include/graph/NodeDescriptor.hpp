/**
 * @file NodeDescriptor.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <unordered_set>
#include <nlohmann/json.hpp>

#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/INode.hpp"
#include "graph/PortTypes.hpp"
#include "graph/NodeFacadeAbi.hpp"

namespace graph {

struct ConfigFieldMetadata {
    std::string name;
    JsonType type{JsonType::Object};
    bool required{false};
};

/**
 * Runtime descriptor for a graph node.
 *
 * This is the descriptor-oriented surface that policies, schema generation,
 * plugin metadata, and future C++26 reflection support can converge on. It is
 * intentionally value-based so callers do not need to know whether the node is
 * native, plugin-backed, or adapter-backed.
 */
struct NodeDescriptor {
    std::string name;
    std::string type;
    std::string description;
    LifecycleState lifecycle_state{LifecycleState::Invalid};
    bool supports_configuration{false};
    std::vector<ConfigFieldMetadata> config_fields;
    std::vector<PortMetadata> input_ports;
    std::vector<PortMetadata> output_ports;
};

struct NodeDescriptorSeed {
    std::string name;
    std::string type;
    std::string description;
    LifecycleState lifecycle_state{LifecycleState::Invalid};
    bool supports_configuration{false};
};

struct RuntimeNodeDescriptorRequest {
    NodeDescriptorSeed seed;
    const IParameterized* parameterized{nullptr};
    std::vector<PortMetadata> input_ports;
    std::vector<PortMetadata> output_ports;
};

/**
 * @class INodeDescriptorProvider
 * @brief INodeDescriptorProvider class.
 */
class INodeDescriptorProvider {
public:
    virtual ~INodeDescriptorProvider() = default;

/**
 * @brief Build runtime descriptor.
 * @param request Parameter for build runtime descriptor.
 * @return Result of the operation.
 */
    virtual NodeDescriptor BuildRuntimeDescriptor(RuntimeNodeDescriptorRequest request) const = 0;
};

template <typename ParameterizedT>
concept ParameterizedDescriptorSource = requires(const ParameterizedT& source, const std::string& name) {
    source.GetParameters().Raw();
    source.GetParameterNames();
    source.GetParameterDescription(name).Raw();
};

inline NodeDescriptor BuildNodeDescriptor(
    NodeDescriptorSeed seed,
    std::vector<ConfigFieldMetadata> config_fields = {},
    std::vector<PortMetadata> input_ports = {},
    std::vector<PortMetadata> output_ports = {}) {
    NodeDescriptor descriptor;
    descriptor.name = std::move(seed.name);
    descriptor.type = std::move(seed.type);
    descriptor.description = std::move(seed.description);
    descriptor.lifecycle_state = seed.lifecycle_state;
    descriptor.supports_configuration = seed.supports_configuration;
    descriptor.config_fields = std::move(config_fields);
    descriptor.input_ports = std::move(input_ports);
    descriptor.output_ports = std::move(output_ports);
    return descriptor;
}

template <typename NodeType>
inline std::vector<ConfigFieldMetadata> BuildConfigFieldMetadataFromType() {
    std::vector<ConfigFieldMetadata> fields;
    if constexpr (requires { NodeType::Fields(); }) {
        for (const auto& field : NodeType::Fields()) {
            fields.push_back(ConfigFieldMetadata{
                .name = std::string(field.name),
                .type = field.type,
                .required = field.required,
            });
        }
    }
    return fields;
}

inline std::string PortDirectionToString(PortDirection direction) {
    return direction == PortDirection::Input ? "input" : "output";
}

inline PortDirection PortDirectionFromString(std::string_view direction) {
    if (direction == "input") {
        return PortDirection::Input;
    }
    return PortDirection::Output;
}

inline PortMetadata ToPortMetadata(const PortInfo& info) {
    return PortMetadata{
        .port_index = info.id,
        .payload_type = std::string(info.type_name),
        .direction = PortDirectionToString(info.direction),
        .port_name = "Port" + std::to_string(info.id)
    };
}

inline PortMetadata ToPortMetadata(const PortMetadataC& info) {
    return PortMetadata{
        .port_index = info.index,
        .payload_type = info.payload_type,
        .direction = info.direction,
        .port_name = info.port_name
    };
}

inline PortMetadataC ToPortMetadataC(const PortMetadata& info) {
    return MakePortMetadataC(
        info.port_index,
        info.port_name,
        info.payload_type,
        info.direction);
}

inline JsonType JsonTypeFromJsonValue(const nlohmann::json& value) {
    if (value.is_string()) {
        return JsonType::String;
    }
    if (value.is_boolean()) {
        return JsonType::Boolean;
    }
    if (value.is_number_integer()) {
        return JsonType::Integer;
    }
    if (value.is_number_float() || value.is_number_unsigned()) {
        return JsonType::Number;
    }
    if (value.is_array()) {
        return JsonType::Array;
    }
    return JsonType::Object;
}

inline JsonType JsonTypeFromSchemaTypeName(std::string_view type_name) {
    if (type_name == "string") {
        return JsonType::String;
    }
    if (type_name == "number") {
        return JsonType::Number;
    }
    if (type_name == "integer") {
        return JsonType::Integer;
    }
    if (type_name == "boolean") {
        return JsonType::Boolean;
    }
    if (type_name == "array") {
        return JsonType::Array;
    }
    return JsonType::Object;
}

template <typename ParameterNamesFn, typename ParameterDescriptionFn>
inline std::vector<ConfigFieldMetadata> BuildConfigFieldMetadataFromParameterized(
    const nlohmann::json& parameter_json,
    ParameterNamesFn&& get_parameter_names,
    ParameterDescriptionFn&& get_parameter_description) {
    std::vector<ConfigFieldMetadata> config_fields;
    std::unordered_set<std::string> seen_fields;

    for (const auto& field_name : get_parameter_names()) {
        const auto metadata_json = get_parameter_description(field_name);

        JsonType field_type = JsonType::Object;
        if (metadata_json.is_object() && metadata_json.contains("type") &&
            metadata_json["type"].is_string()) {
            field_type = JsonTypeFromSchemaTypeName(metadata_json["type"].template get<std::string>());
        } else if (parameter_json.is_object() && parameter_json.contains(field_name)) {
            field_type = JsonTypeFromJsonValue(parameter_json[field_name]);
        }

        const bool required =
            metadata_json.is_object() && metadata_json.contains("required") &&
            metadata_json["required"].is_boolean() && metadata_json["required"].template get<bool>();

        config_fields.push_back(ConfigFieldMetadata{
            .name = field_name,
            .type = field_type,
            .required = required,
        });
        seen_fields.insert(field_name);
    }

    if (parameter_json.is_object()) {
        for (const auto& [field_name, field_value] : parameter_json.items()) {
            if (seen_fields.contains(field_name)) {
                continue;
            }

            config_fields.push_back(ConfigFieldMetadata{
                .name = field_name,
                .type = JsonTypeFromJsonValue(field_value),
                .required = false,
            });
        }
    }

    return config_fields;
}

template <typename ParameterizedT>
requires ParameterizedDescriptorSource<ParameterizedT>
inline NodeDescriptor BuildRuntimeNodeDescriptor(
    NodeDescriptorSeed seed,
    const ParameterizedT* parameterized,
    std::vector<PortMetadata> input_ports,
    std::vector<PortMetadata> output_ports) {
    std::vector<ConfigFieldMetadata> config_fields;
    if (parameterized) {
        try {
            config_fields = BuildConfigFieldMetadataFromParameterized(
                parameterized->GetParameters().Raw(),
                [parameterized]() {
                    return parameterized->GetParameterNames();
                },
                [parameterized](const std::string& field_name) {
                    return parameterized->GetParameterDescription(field_name).Raw();
                });
        } catch (...) {
            // Best-effort descriptor enrichment only.
        }
    }

    return BuildNodeDescriptor(
        std::move(seed),
        std::move(config_fields),
        std::move(input_ports),
        std::move(output_ports));
}

inline NodeDescriptor BuildRuntimeNodeDescriptor(
    NodeDescriptorSeed seed,
    std::vector<PortMetadata> input_ports,
    std::vector<PortMetadata> output_ports) {
    return BuildNodeDescriptor(
        std::move(seed),
        {},
        std::move(input_ports),
        std::move(output_ports));
}

/**
 * @class DefaultNodeDescriptorProvider
 * @brief Default node descriptor provider implementation for GraphX.
 */
class DefaultNodeDescriptorProvider final : public INodeDescriptorProvider {
public:
    NodeDescriptor BuildRuntimeDescriptor(RuntimeNodeDescriptorRequest request) const override {
        return BuildRuntimeNodeDescriptor(
            std::move(request.seed),
            request.parameterized,
            std::move(request.input_ports),
            std::move(request.output_ports));
    }
};

inline const INodeDescriptorProvider& GetDefaultNodeDescriptorProvider() {
    static const DefaultNodeDescriptorProvider provider;
    return provider;
}

}  // namespace graph
