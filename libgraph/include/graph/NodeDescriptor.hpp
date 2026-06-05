// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <string_view>
#include <string>
#include <vector>

#include "config/Config.hpp"
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

}  // namespace graph
