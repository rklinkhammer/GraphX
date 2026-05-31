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