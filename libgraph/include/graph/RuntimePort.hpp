// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string>

#include "graph/PortSpec.hpp"

namespace graph {

class IPortFunction;

enum class RuntimePortLookupError {
    PortNotFound = 1,
    MetadataUnavailable = 2,
};

enum class RuntimePortConnectError {
    DirectionMismatch = 1,
    PayloadTypeMismatch = 2,
    NullDestination = 3,
    TransportTypeMismatch = 4,
    CapacityInvalid = 5,
    TransferFailed = 6,
};

struct RuntimePortDescriptor {
    std::size_t id;
    std::string name;
    PortDirection direction;
    std::string payload_type;
    std::string transport_type;
};

struct RuntimePortHandle {
    std::size_t node_index;
    RuntimePortDescriptor descriptor;
    std::shared_ptr<IPortFunction> owned_port;
    IPortFunction* port;
};

}  // namespace graph