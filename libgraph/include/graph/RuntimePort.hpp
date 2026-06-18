/**
 * @file RuntimePort.hpp
 * @brief Runtime Port Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

/**

 * @class IPortFunction

 * @brief Iport Function type.

 *

 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.

 */

class IPortFunction;

/**

 * @enum RuntimePortLookupError

 * @brief Runtime Port Lookup Error values.

 *

 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

enum class RuntimePortLookupError {
    PortNotFound = 1,
    MetadataUnavailable = 2,
};

/**

 * @enum RuntimePortConnectError

 * @brief Runtime Port Connect Error values.

 *

 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

enum class RuntimePortConnectError {
    DirectionMismatch = 1,
    PayloadTypeMismatch = 2,
    NullDestination = 3,
    TransportTypeMismatch = 4,
    CapacityInvalid = 5,
    TransferFailed = 6,
};

/**

 * @struct RuntimePortDescriptor

 * @brief Runtime Port Descriptor data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct RuntimePortDescriptor {
    std::size_t id;
    std::string name;
    PortDirection direction;
    std::string payload_type;
    std::string transport_type;
};

/**

 * @struct RuntimePortHandle

 * @brief Runtime Port Handle data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct RuntimePortHandle {
    std::size_t node_index;
    RuntimePortDescriptor descriptor;
    std::shared_ptr<IPortFunction> owned_port;
    IPortFunction* port;
};

}  // namespace graph
