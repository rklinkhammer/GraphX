/**
 * @file graph.port_metadata.ixx
 * @brief Graph.port Metadata Graph runtime support.
 *
 * @details Provides GraphX runtime support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */

module;

#include "graph/PortTypes.hpp"

export module graph.port_metadata;

export namespace graph {
	using ::graph::TypeList;
	using ::graph::RepeatType_t;
	using ::graph::Port;
	using ::graph::MakePorts;
	using ::graph::PortInfo;
	using ::graph::make_port_info;
	using ::graph::build_port_table;
	using ::graph::PortMetadata;
	using ::graph::CountPortsByDirection;
	using ::graph::NodePortDescriptor;
	using ::graph::HasCompileTimePortCounts;
	using ::graph::MakePortMetadata;
	using ::graph::MakePortMetadataForDirection;
	using ::graph::HasPorts;
}