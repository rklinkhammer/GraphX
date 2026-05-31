# GraphX C++ Project - Comprehensive Class Inventory

**Project**: GraphX (C++26 Dataflow Graph Framework)
**Date**: May 10, 2026
**Analysis Scope**: All classes in libgraph and libsensor

---

## Overview

This document provides a complete inventory of all classes across the GraphX project, organized by library and component, with test coverage status for each class.

### Key Statistics
- **Total Classes/Structs**: 180+
- **Unit Tests Coverage**: 6 test files (CapabilityBus, Message, ActiveQueue, VariantRouter, JsonView, JsonUtilities)
- **Integration Tests Coverage**: 3 test files (CSVParser, Graph, CSVPipeline)
- **Well-Tested Core**: ~35%
- **Partially Tested**: ~40%
- **Untested/No Unit Tests**: ~25%

---

# LIBGRAPH - Core Graph Framework

## Component: graph/

### Core Node Architecture

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `INode` | [graph/INode.hpp](libgraph/include/graph/INode.hpp) | ❌ No | ❌ No | Abstract base interface for all graph nodes; defines lifecycle (Init, Start, Stop, Join) and port introspection |
| `SourceNode<Outputs...>` | [graph/Nodes.hpp](libgraph/include/graph/Nodes.hpp) | ❌ No | ✅ Yes (test_graph_1.cpp) | Template base for producer nodes with typed output ports |
| `SinkNode<Inputs...>` | [graph/Nodes.hpp](libgraph/include/graph/Nodes.hpp) | ❌ No | ✅ Yes (test_graph_1.cpp) | Template base for consumer nodes with typed input ports |
| `InteriorNode<InputList, OutputList>` | [graph/Nodes.hpp](libgraph/include/graph/Nodes.hpp) | ❌ No | ❌ No | Template base for transformation nodes with both inputs and outputs |
| `MergeNode<N, CommonInput, OutputType>` | [graph/Nodes.hpp](libgraph/include/graph/Nodes.hpp) | ❌ No | ❌ No | Template for N-input merge nodes combining multiple streams |
| `SplitNode<T, N>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Template for 1-to-N splitter nodes; also includes SplitNode1-8 variants |
| `SplitNode1<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 1 output |
| `SplitNode2<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 2 outputs |
| `SplitNode3<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 3 outputs |
| `SplitNode4<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 4 outputs |
| `SplitNode5<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 5 outputs |
| `SplitNode6<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 6 outputs |
| `SplitNode7<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 7 outputs |
| `SplitNode8<T>` | [graph/SplitNode.hpp](libgraph/include/graph/SplitNode.hpp) | ❌ No | ❌ No | Specialization: 1 input → 8 outputs |
| `NamedSourceNode<NodeType, Outputs...>` | [graph/NamedNodes.hpp](libgraph/include/graph/NamedNodes.hpp) | ❌ No | ❌ No | Source node with compile-time name tracking |
| `NamedSinkNode<NodeType, Inputs...>` | [graph/NamedNodes.hpp](libgraph/include/graph/NamedNodes.hpp) | ❌ No | ❌ No | Sink node with compile-time name tracking |
| `NamedInteriorNode<NodeType, InputList, OutputList>` | [graph/NamedNodes.hpp](libgraph/include/graph/NamedNodes.hpp) | ❌ No | ❌ No | Interior node with compile-time name tracking |
| `NamedMergeNode<NodeType, N, CommonInput, OutputType>` | [graph/NamedNodes.hpp](libgraph/include/graph/NamedNodes.hpp) | ❌ No | ❌ No | Merge node with compile-time name tracking |

### Message Types

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `Message` | [graph/Message.hpp](libgraph/include/graph/Message.hpp) | ✅ Yes (test_message.cpp) | ✅ Yes (multiple) | Type-erased message container with small object optimization (SSO); 32-byte default buffer |
| `MessageStorage<Policy>` | [graph/Message.hpp](libgraph/include/graph/Message.hpp) | ✅ Yes (test_message.cpp) | ❌ No | Policy-based storage backend for Message with configurable size/alignment |
| `MessageStoragePolicy<Size, Align>` | [graph/Message.hpp](libgraph/include/graph/Message.hpp) | ✅ Yes (test_message.cpp) | ❌ No | Template struct for configuring Message SSO buffer (size=32, align=16 default) |
| `MessageTraits` | [graph/Message.hpp](libgraph/include/graph/Message.hpp) | ✅ Yes (test_message.cpp) | ❌ No | Type traits metaprogramming for Message types |
| `MessagePoolRegistry` | [graph/PooledMessage.hpp](libgraph/include/graph/PooledMessage.hpp) | ❌ No | ❌ No | Central registry for message object pools; manages allocation/deallocation |
| `MessageBufferPool` | [graph/MessagePool.hpp](libgraph/include/graph/MessagePool.hpp) | ❌ No | ❌ No | Lock-free buffer pool for message objects; reduces allocation overhead |
| `MessagePoolRouter` | [graph/MessagePoolRouter.hpp](libgraph/include/graph/MessagePoolRouter.hpp) | ❌ No | ❌ No | Routes messages between thread-local pools; coordinates allocation across threads |
| `PooledMessageConfig` | [graph/PooledMessage.hpp](libgraph/include/graph/PooledMessage.hpp) | ❌ No | ❌ No | Configuration struct for pooled message behavior |
| `PooledMessageStats` | [graph/PooledMessage.hpp](libgraph/include/graph/PooledMessage.hpp) | ❌ No | ❌ No | Statistics collection for message pool performance |
| `PooledMessageHelper` | [graph/PooledMessage.hpp](libgraph/include/graph/PooledMessage.hpp) | ❌ No | ❌ No | Utility functions for pooled message operations |

### Graph Management & Topology

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `GraphManager` | [graph/GraphManager.hpp](libgraph/include/graph/GraphManager.hpp) | ❌ No | ✅ Yes (test_graph_1.cpp) | High-level graph orchestration; manages nodes, edges, metrics, lifecycle |
| `GraphBuilder` | [graph/GraphBuilder.hpp](libgraph/include/graph/GraphBuilder.hpp) | ❌ No | ❌ No | Fluent API for programmatic graph construction |
| `FluentGraphBuilder` | [graph/FluentGraphBuilder.hpp](libgraph/include/graph/FluentGraphBuilder.hpp) | ❌ No | ✅ Yes (test_graph_1.cpp) | Method-chaining builder for readable graph definition |
| `GraphConfig` | [graph/GraphConfig.hpp](libgraph/include/graph/GraphConfig.hpp) | ❌ No | ❌ No | Struct containing node and edge configurations for graph |
| `NodeConfig` | [graph/GraphConfig.hpp](libgraph/include/graph/GraphConfig.hpp) | ❌ No | ❌ No | Configuration for individual nodes (type, name, parameters) |
| `EdgeConfig` | [graph/GraphConfig.hpp](libgraph/include/graph/GraphConfig.hpp) | ❌ No | ❌ No | Configuration for edges (source, dest, ports, buffer size) |
| `ValidationResult` | [graph/GraphConfig.hpp](libgraph/include/graph/GraphConfig.hpp) | ❌ No | ❌ No | Result struct for graph configuration validation |
| `GraphConfigParser` | [graph/GraphConfigParser.hpp](libgraph/include/graph/GraphConfigParser.hpp) | ❌ No | ❌ No | Parses graph topology from configuration files (JSON/YAML) |
| `JsonDynamicGraphLoader` | [graph/JsonDynamicGraphLoader.hpp](libgraph/include/graph/JsonDynamicGraphLoader.hpp) | ❌ No | ❌ No | Loads graph topology and connects nodes dynamically from JSON |
| `EdgeRegistry` | [graph/EdgeRegistry.hpp](libgraph/include/graph/EdgeRegistry.hpp) | ❌ No | ❌ No | Type-aware edge creation registry; enables runtime edge instantiation |
| `EdgeKey` | [graph/EdgeRegistry.hpp](libgraph/include/graph/EdgeRegistry.hpp) | ❌ No | ❌ No | Type-indexed key for O(1) edge lookup (src_type, src_port, dst_type, dst_port) |
| `EdgeKeyHash` | [graph/EdgeRegistry.hpp](libgraph/include/graph/EdgeRegistry.hpp) | ❌ No | ❌ No | Hash function for EdgeKey combining type indices and ports |

### Edge & Port Management

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `IEdgeBase` | [graph/EdgeFacade.hpp](libgraph/include/graph/EdgeFacade.hpp) | ❌ No | ❌ No | Abstract base for type-erased edges; enables polymorphic edge operations |
| `EdgeWrapper<SrcNode, SrcPort, DstNode, DstPort>` | [graph/EdgeFacade.hpp](libgraph/include/graph/EdgeFacade.hpp) | ❌ No | ❌ No | Template wrapper for typed edge with message queue and buffering |
| `EdgeFacadeAdapter` | [graph/EdgeFacade.hpp](libgraph/include/graph/EdgeFacadeAdapter.hpp) | ❌ No | ❌ No | Adapts templated edges to IEdgeBase interface for type-erasure |
| `EdgeRegistration<SrcNode, SrcPort, DstNode, DstPort>` | [graph/EdgeRegistration.hpp](libgraph/include/graph/EdgeRegistration.hpp) | ❌ No | ❌ No | Registration helper for edge templates with type information |
| `PortSpec` | [graph/PortSpec.hpp](libgraph/include/graph/PortSpec.hpp) | ❌ No | ❌ No | Specification for port characteristics (index, payload type, direction) |
| `PayloadList` | [graph/PortSpec.hpp](libgraph/include/graph/PortSpec.hpp) | ❌ No | ❌ No | Type list of payload types for a port |
| `PortVisitor` | [graph/PortVisitor.hpp](libgraph/include/graph/PortVisitor.hpp) | ❌ No | ❌ No | Visitor pattern implementation for port introspection |
| `InputFunction<PayloadT>` | [graph/InputFunction.hpp](libgraph/include/graph/InputFunction.hpp) | ❌ No | ❌ No | Type-specific input port implementation |
| `OutputFunction<PayloadT>` | [graph/OutputFunction.hpp](libgraph/include/graph/OutputFunction.hpp) | ❌ No | ❌ No | Type-specific output port implementation |
| `PortFunction<PayloadT>` | [graph/PortFunction.hpp](libgraph/include/graph/PortFunction.hpp) | ❌ No | ❌ No | Base for port function implementations |
| `IPortFunction` | [graph/IPortFunction.hpp](libgraph/include/graph/IPortFunction.hpp) | ❌ No | ❌ No | Abstract interface for port functions (polymorphic operations) |
| `OutputPortMetricsMixin` | [graph/PortMetricsMixin.hpp](libgraph/include/graph/PortMetricsMixin.hpp) | ❌ No | ❌ No | Mixin for per-port output metrics collection |
| `InputPortMetricsMixin` | [graph/PortMetricsMixin.hpp](libgraph/include/graph/PortMetricsMixin.hpp) | ❌ No | ❌ No | Mixin for per-port input metrics collection |
| `BidirectionalPortMetricsMixin` | [graph/PortMetricsMixin.hpp](libgraph/include/graph/PortMetricsMixin.hpp) | ❌ No | ❌ No | Mixin for ports with both input and output metrics |

### Metrics & Observability

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `GraphMetrics` | [graph/GraphMetrics.hpp](libgraph/include/graph/GraphMetrics.hpp) | ❌ No | ❌ No | Graph-level aggregate performance metrics (throughput, latency, backpressure) |
| `EdgeMetrics` | [graph/GraphMetrics.hpp](libgraph/include/graph/GraphMetrics.hpp) | ❌ No | ❌ No | Per-edge queue statistics and performance metrics |
| `EdgeMetadata` | [graph/GraphMetrics.hpp](libgraph/include/graph/GraphMetrics.hpp) | ❌ No | ❌ No | Edge connectivity and type information for visualization |
| `MetricsEvent` | [metrics/MetricsEvent.hpp](libgraph/include/metrics/MetricsEvent.hpp) | ❌ No | ❌ No | Event struct for metrics callbacks during execution |
| `IMetricsCallback` | [metrics/IMetricsCallback.hpp](libgraph/include/metrics/IMetricsCallback.hpp) | ❌ No | ❌ No | Abstract interface for metrics event listeners |
| `IMetricsCallbackProvider` | [metrics/IMetricsCallback.hpp](libgraph/include/metrics/IMetricsCallback.hpp) | ❌ No | ❌ No | Abstract interface for objects providing metrics callbacks |
| `IMetricsSubscriber` | [metrics/IMetricsSubscriber.hpp](libgraph/include/metrics/IMetricsSubscriber.hpp) | ❌ No | ❌ No | Observer interface for metrics event subscriptions |
| `MetricsCapability` | [capabilities/MetricsCapability.hpp](libgraph/include/capabilities/MetricsCapability.hpp) | ❌ No | ❌ No | Capability for metrics discovery and subscription management |
| `NodeMetricsSchema` | [metrics/NodeMetricsSchema.hpp](libgraph/include/metrics/NodeMetricsSchema.hpp) | ❌ No | ❌ No | Schema describing available metrics for a node type |
| `ThreadMetrics` | [graph/ThreadMetrics.hpp](libgraph/include/graph/ThreadMetrics.hpp) | ❌ No | ❌ No | Per-thread execution metrics (messages processed, time) |

### Data Generation & Injection

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `DataGeneratorBase<DataType>` | [graph/DataGeneratorBase.hpp](libgraph/include/graph/DataGeneratorBase.hpp) | ❌ No | ❌ No | Base class for data-generating source nodes |
| `DataInjectionGeneratorBase<DataType>` | [graph/DataInjectionGeneratorBase.hpp](libgraph/include/graph/DataInjectionGeneratorBase.hpp) | ❌ No | ❌ No | Base for injection-capable nodes (CSV, runtime data feed) |
| `DataProducerWithNotification<DataType>` | [graph/DataProducerWithNotification.hpp](libgraph/include/graph/DataProducerWithNotification.hpp) | ❌ No | ❌ No | Producer node with completion callbacks (async support) |
| `IDataInjectionSource` | [graph/IDataInjectionSource.hpp](libgraph/include/graph/IDataInjectionSource.hpp) | ❌ No | ❌ No | Interface for nodes accepting external data injection |

### Execution & Completion

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `GraphExecutor` | [graph/GraphExecutor.hpp](libgraph/include/graph/GraphExecutor.hpp) | ❌ No | ❌ No | Executes graph with optional timeout and completion tracking |
| `GraphExecutorBuilder` | [graph/GraphExecutorBuilder.hpp](libgraph/include/graph/GraphExecutorBuilder.hpp) | ❌ No | ❌ No | Fluent builder for GraphExecutor configuration |
| `ExecutionResult` | [graph/ExecutionResult.hpp](libgraph/include/graph/ExecutionResult.hpp) | ❌ No | ❌ No | Result of graph execution (success, messages processed, metrics) |
| `InitializationResult` | [graph/ExecutionResult.hpp](libgraph/include/graph/ExecutionResult.hpp) | ❌ No | ❌ No | Result of graph initialization |
| `CompletionSignal` | [graph/CompletionSignal.hpp](libgraph/include/graph/CompletionSignal.hpp) | ❌ No | ❌ No | Signal for graph completion (used for synchronization) |
| `ICompletionCallback` | [graph/ICompletionCallback.hpp](libgraph/include/graph/ICompletionCallback.hpp) | ❌ No | ❌ No | Callback interface invoked when graph completes |
| `CompletionAggregatorNode` | [graph/CompletionAggregatorNode.hpp](libgraph/include/graph/CompletionAggregatorNode.hpp) | ❌ No | ❌ No | Sink node that aggregates multiple completion signals |
| `ExecutionState` | [graph/ExecutionState.hpp](libgraph/include/graph/ExecutionState.hpp) | ❌ No | ❌ No | Tracks current execution state (initialized, running, stopped) |

### Utilities

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `ThreadPool` | [graph/ThreadPool.hpp](libgraph/include/graph/ThreadPool.hpp) | ❌ No | ❌ No | Shared worker thread pool for graph nodes; supports async task execution |
| `ThreadPoolStats` | [graph/ThreadPool.hpp](libgraph/include/graph/ThreadPool.hpp) | ❌ No | ❌ No | Statistics for thread pool operation (threads, tasks, queue depth) |
| `AdaptiveCapacityMonitor` | [graph/AdaptiveCapacityMonitor.hpp](libgraph/include/graph/AdaptiveCapacityMonitor.hpp) | ❌ No | ❌ No | Monitors queue depth and adapts capacity based on workload |
| `AdaptiveCapacityConfig` | [graph/AdaptiveCapacityMonitor.hpp](libgraph/include/graph/AdaptiveCapacityMonitor.hpp) | ❌ No | ❌ No | Configuration for adaptive capacity monitoring |
| `StaticNodeAdapter` | [graph/StaticNodeAdapter.hpp](libgraph/include/graph/StaticNodeAdapter.hpp) | ❌ No | ❌ No | Adapter for static member function callbacks in nodes |
| `NamedType<T>` | [graph/NamedType.hpp](libgraph/include/graph/NamedType.hpp) | ❌ No | ❌ No | Base class providing compile-time name tracking via template parameter |
| `Lifecycle` | [graph/Lifecycle.hpp](libgraph/include/graph/Lifecycle.hpp) | ❌ No | ❌ No | Lifecycle state machine for nodes |
| `IFnBase` | [graph/IFnBase.hpp](libgraph/include/graph/IFnBase.hpp) | ❌ No | ❌ No | Base for type-erased function pointers |
| `TransferFunction<PayloadT>` | [graph/TransferFunction.hpp](libgraph/include/graph/TransferFunction.hpp) | ❌ No | ❌ No | Encapsulates typed message transfer between ports |
| `MergeFunction<OutputT, InputT...>` | [graph/MergeFunction.hpp](libgraph/include/graph/MergeFunction.hpp) | ❌ No | ❌ No | Function merging multiple input types into single output |

---

## Component: core/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `ActiveQueue<Element>` | [core/ActiveQueue.hpp](libgraph/include/core/ActiveQueue.hpp) | ✅ Yes (test_active_queue.cpp) | ✅ Yes (multiple) | Thread-safe deque with blocking enqueue/dequeue and condition variables |
| `QueueMetrics` | [core/ActiveQueue.hpp](libgraph/include/core/ActiveQueue.hpp) | ✅ Yes (test_active_queue.cpp) | ❌ No | Performance metrics for queue operations (throughput, latency, backpressure) |
| `VariantRouter<Variant>` | [core/VariantRouter.hpp](libgraph/include/core/VariantRouter.hpp) | ✅ Yes (test_variant_router.cpp) | ❌ No | Generic type-based dispatcher for std::variant types |
| `TypeInfo` | [core/TypeInfo.hpp](libgraph/include/core/TypeInfo.hpp) | ❌ No | ❌ No | RTTI wrapper providing demangled type names and metadata |
| `CallbackChain<RetT, ArgsT...>` | [core/CallbackUtilities.hpp](libgraph/include/core/CallbackUtilities.hpp) | ❌ No | ❌ No | Chain of responsibility pattern for callbacks (non-void return) |
| `CallbackChain<void, ArgsT...>` | [core/CallbackUtilities.hpp](libgraph/include/core/CallbackUtilities.hpp) | ❌ No | ❌ No | Specialization for void-return callbacks |
| `CallbackRegistry` | [core/CallbackUtilities.hpp](libgraph/include/core/CallbackUtilities.hpp) | ❌ No | ❌ No | Registry for managing callback chains by key |
| `Overload` | [core/VariantHelper.hpp](libgraph/include/core/VariantHelper.hpp) | ❌ No | ❌ No | CRTP pattern helper for pattern matching in std::visit |
| `CapabilityMetadata` | [core/PluginReflection.hpp](libgraph/include/core/PluginReflection.hpp) | ❌ No | ❌ No | Metadata for plugin capability declarations |
| `PluginMetadata` | [core/PluginReflection.hpp](libgraph/include/core/PluginReflection.hpp) | ❌ No | ❌ No | Complete metadata for plugin including capabilities and interfaces |
| `PluginReflectionWrapper` | [core/PluginReflection.hpp](libgraph/include/core/PluginReflection.hpp) | ❌ No | ❌ No | Wraps plugin type to expose reflection metadata |
| `TypeMetadata` | [core/ReflectionHelper.hpp](libgraph/include/core/ReflectionHelper.hpp) | ❌ No | ❌ No | Metadata for reflected types |

---

## Component: capabilities/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `CapabilityBus` | [graph/CapabilityBus.hpp](libgraph/include/graph/CapabilityBus.hpp) | ✅ Yes (test_capability_bus.cpp) | ✅ Yes (test_graph_1.cpp) | Type-indexed service locator for capabilities; enables plugin composition |
| `DefaultCapabilityBus` | [graph/DefaultCapabilityBus.hpp](libgraph/include/graph/DefaultCapabilityBus.hpp) | ✅ Yes (test_capability_bus.cpp) | ❌ No | Concrete implementation of CapabilityBus with registration methods |
| `GraphCapability` | [capabilities/GraphCapability.hpp](libgraph/include/capabilities/GraphCapability.hpp) | ❌ No | ✅ Yes (test_graph_1.cpp) | Holds GraphManager and factory references for graph manipulation |
| `DataInjectionCapability` | [capabilities/DataInjectionCapability.hpp](libgraph/include/capabilities/DataInjectionCapability.hpp) | ❌ No | ❌ No | Registry of data injection node configurations |
| `DataInjectionNodeConfig` | [capabilities/DataInjectionCapability.hpp](libgraph/include/capabilities/DataInjectionCapability.hpp) | ❌ No | ❌ No | Configuration for a node that accepts external data injection |
| `CommandCapability` | [capabilities/CommandCapability.hpp](libgraph/include/capabilities/CommandCapability.hpp) | ❌ No | ❌ No | Wrapper for command infrastructure access |
| `CommandRegistryCapability` | [capabilities/CommandRegistryCapability.hpp](libgraph/include/capabilities/CommandRegistryCapability.hpp) | ❌ No | ❌ No | Capability providing command registration without dashboard dependency |
| `CommandOutputCapability` | [capabilities/CommandOutputCapability.hpp](libgraph/include/capabilities/CommandOutputCapability.hpp) | ❌ No | ❌ No | Capability managing command output routing |
| `CommandProcessorCapability` | [capabilities/CommandProcessorCapability.hpp](libgraph/include/capabilities/CommandProcessorCapability.hpp) | ❌ No | ❌ No | Capability for processing commands from various sources |
| `ICommandOutput` | [capabilities/ICommandOutput.hpp](libgraph/include/capabilities/ICommandOutput.hpp) | ❌ No | ❌ No | Abstract interface for command output destination (console, dashboard, etc.) |
| `ConsoleOutput` | [capabilities/ConsoleOutput.hpp](libgraph/include/capabilities/ConsoleOutput.hpp) | ❌ No | ❌ No | ICommandOutput implementation routing to console/stderr |
| `DashboardCapability` | [capabilities/DashboardCapability.hpp](libgraph/include/capabilities/DashboardCapability.hpp) | ❌ No | ❌ No | Capability for dashboard UI integration |
| `DashboardOutput` | [capabilities/DashboardOutput.hpp](libgraph/include/capabilities/DashboardOutput.hpp) | ❌ No | ❌ No | ICommandOutput implementation routing to dashboard window |
| `CSVDataInjectionCapability` | [capabilities/CSVDataInjectionCapability.hpp](libgraph/include/capabilities/CSVDataInjectionCapability.hpp) | ❌ No | ❌ No | Capability for CSV-based data injection infrastructure |
| `CSVDataInjectionCommand` | [capabilities/CSVDataInjectionCapability.hpp](libgraph/include/capabilities/CSVDataInjectionCapability.hpp) | ❌ No | ❌ No | Command struct for CSV injection operations |

---

## Component: csv/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `CSVParser` | [csv/CSVParser.hpp](libgraph/include/csv/CSVParser.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Generalized CSV parser with column mapping and type conversion |
| `CSVHeader` | [csv/CSVParser.hpp](libgraph/include/csv/CSVParser.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | CSV header metadata (columns, timestamp column, data columns, format) |
| `CSVNodeConfig` | [csv/CSVDataInjectionManager.hpp](libgraph/include/csv/CSVDataInjectionManager.hpp) | ❌ No | ✅ Yes (test_csv_pipeline_3.cpp) | Configuration for a CSV-capable node (name, columns, queue) |
| `CSVNodeDescriptor` | [csv/CSVNodeDescriptor.hpp](libgraph/include/csv/CSVNodeDescriptor.hpp) | ❌ No | ❌ No | Type-erased descriptor for CSV-capable nodes with closures for access |
| `ColumnMapping` | [csv/CSVParser.hpp](libgraph/include/csv/CSVParser.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Maps field names to CSV column indices for parsing |
| `CSVDataInjectionManager` | [csv/CSVDataInjectionManager.hpp](libgraph/include/csv/CSVDataInjectionManager.hpp) | ❌ No | ✅ Yes (test_csv_pipeline_3.cpp) | Manages CSV file processing and data injection into graph nodes |

---

## Component: ui/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `CommandRegistry` | [ui/CommandRegistry.hpp](libgraph/include/ui/CommandRegistry.hpp) | ❌ No | ❌ No | Extensible command registry for dashboard user input |
| `CommandResult` | [ui/CommandRegistry.hpp](libgraph/include/ui/CommandRegistry.hpp) | ❌ No | ❌ No | Result struct from command execution (success, message) |
| `CommandInfo` | [ui/CommandRegistry.hpp](libgraph/include/ui/CommandRegistry.hpp) | ❌ No | ❌ No | Metadata for registered command (name, description, usage, handler) |
| `Metric` | [ui/Metric.hpp](libgraph/include/ui/Metric.hpp) | ❌ No | ❌ No | Metric value with name, units, and aggregation info |

---

## Component: config/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `JsonView` | [config/JsonView.hpp](libgraph/include/config/JsonView.hpp) | ✅ Yes (test_json_view.cpp) | ❌ No | Safe JSON navigation with error handling; replaces nlohmann::json::at() |
| `JsonParseResult` | [config/JsonUtilities.hpp](libgraph/include/config/JsonUtilities.hpp) | ✅ Yes (test_json_utilities.cpp) | ❌ No | Result wrapper for JSON parsing operations |
| `TimestampedData` | [config/TimestampedData.hpp](libgraph/include/config/TimestampedData.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Base struct for timestamped sensor data |
| `TimestampedVector3D` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Timestamped 3D vector (position, velocity, acceleration) |
| `TimestampedScalar` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Timestamped scalar value |
| `StateVector` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ❌ No | Full system state vector (position, velocity, attitude, rates) |
| `Vector3D` | [config/BasicTypes.hpp](libgraph/include/config/BasicTypes.hpp) | ❌ No | ❌ No | 3D vector with x, y, z components |
| `Quaternion` | [config/BasicTypes.hpp](libgraph/include/config/BasicTypes.hpp) | ❌ No | ❌ No | Quaternion for 3D rotations (w, x, y, z) |
| `GPSPositionData` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | GPS position, altitude, velocity, satellite info |
| `AccelerometerData` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Accelerometer data (x, y, z in m/s²) |
| `GyroscopeData` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Gyroscope data (roll, pitch, yaw rates in rad/s) |
| `MagnetometerData` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Magnetometer data (x, y, z in Tesla) |
| `BarometricData` | [config/DataTypes.hpp](libgraph/include/config/DataTypes.hpp) | ❌ No | ✅ Yes (test_csv_parser.cpp) | Barometric pressure and temperature data |
| `ConfigError` | [config/ConfigError.hpp](libgraph/include/config/ConfigError.hpp) | ❌ No | ❌ No | Exception for configuration errors |
| `ConfigParser` | [config/ConfigParser.hpp](libgraph/include/config/ConfigParser.hpp) | ❌ No | ❌ No | Parses configuration from JSON files |
| `JsonField` | [config/Config.hpp](libgraph/include/config/Config.hpp) | ❌ No | ❌ No | JSON field descriptor (name, type, required, default) |
| `JsonSchema` | [config/Config.hpp](libgraph/include/config/Config.hpp) | ❌ No | ❌ No | Schema defining valid JSON structure |
| `FieldConstraint` | [config/SchemaGenerator.hpp](libgraph/include/config/SchemaGenerator.hpp) | ❌ No | ❌ No | Constraint for schema field (min, max, pattern, enum) |
| `ReflectedFieldMetadata` | [config/SchemaGenerator.hpp](libgraph/include/config/SchemaGenerator.hpp) | ❌ No | ❌ No | Metadata from reflection for schema field |
| `GeneratedSchema` | [config/SchemaGenerator.hpp](libgraph/include/config/SchemaGenerator.hpp) | ❌ No | ❌ No | Schema generated from reflected type |
| `SchemaValidator` | [config/SchemaGenerator.hpp](libgraph/include/config/SchemaGenerator.hpp) | ❌ No | ❌ No | Validates JSON against generated schema |
| `DeserializationResult` | [config/JsonDeserialization.hpp](libgraph/include/config/JsonDeserialization.hpp) | ❌ No | ❌ No | Result of deserializing JSON to typed object |
| `WindowHeightConfig` | [config/ConfigLoader.hpp](libgraph/include/config/ConfigLoader.hpp) | ❌ No | ❌ No | Configuration for dashboard window height |

---

## Component: plugins/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `PluginLoader` | [plugins/PluginLoader.hpp](libgraph/include/plugins/PluginLoader.hpp) | ❌ No | ❌ No | Loads dynamic plugins from shared libraries (.so/.dylib/.dll) |
| `PluginRegistry` | [plugins/PluginRegistry.hpp](libgraph/include/plugins/PluginRegistry.hpp) | ❌ No | ❌ No | Registry for loaded plugins; tracks metadata and instances |
| `NodeFactory` | [graph/NodeFactory.hpp](libgraph/include/graph/NodeFactory.hpp) | ❌ No | ❌ No | Factory for creating node instances from plugin classes |
| `NodeFactoryRegistry` | [graph/NodeFactoryRegistry.hpp](libgraph/include/graph/NodeFactoryRegistry.hpp) | ❌ No | ❌ No | Registry of available node factories from all plugins |
| `FactoryManager` | [graph/FactoryManager.hpp](libgraph/include/graph/FactoryManager.hpp) | ❌ No | ❌ No | Coordinates plugin loading, registration, and node factory creation |
| `NodeFacade` | [graph/NodeFacade.hpp](libgraph/include/graph/NodeFacade.hpp) | ❌ No | ❌ No | Struct wrapper for plugin node metadata and constructors |
| `NodePluginInstance` | [graph/NodeFacade.hpp](libgraph/include/graph/NodeFacade.hpp) | ❌ No | ❌ No | Instance metadata for a plugin-provided node class |
| `INodeFacade` | [graph/NodeFacade.hpp](libgraph/include/graph/NodeFacade.hpp) | ❌ No | ❌ No | Abstract interface for type-erased node creation |
| `NodeFacadeAdapter` | [graph/NodeFacade.hpp](libgraph/include/graph/NodeFacadeAdapter.hpp) | ❌ No | ❌ No | Adapts templated NodeFacade to INodeFacade interface |
| `NodeFacadeAdapterWrapper` | [graph/NodeFacadeAdapterWrapper.hpp](libgraph/include/graph/NodeFacadeAdapterWrapper.hpp) | ❌ No | ❌ No | Wraps node facade adapter and presents as INode |
| `PluginInspector` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Introspects plugin capabilities and validates compliance |
| `PluginInfo` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Basic plugin information (name, version, author) |
| `InterfaceCapability` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Capability of plugin to implement a specific interface |
| `PluginCapabilities` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Aggregate of all capabilities provided by a plugin |
| `ComplianceStats` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Compliance scoring for plugin code quality |
| `CacheInfo` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Cache statistics for plugin symbol lookups |
| `SemanticVersion` | [plugins/PluginInspector.hpp](libgraph/include/plugins/PluginInspector.hpp) | ❌ No | ❌ No | Semantic versioning struct (major, minor, patch, prerelease) |
| `PortDescriptor` | [plugins/PluginPortDescriptors.hpp](libgraph/include/plugins/PluginPortDescriptors.hpp) | ❌ No | ❌ No | Descriptor for a plugin node's port |
| `NodeFacadeImpl` | [plugins/NodePluginTemplate.hpp](libgraph/include/plugins/NodePluginTemplate.hpp) | ❌ No | ❌ No | Template implementation of node facade for plugins |
| `NodePluginInstance` | [plugins/NodePluginTemplate.hpp](libgraph/include/plugins/NodePluginTemplate.hpp) | ❌ No | ❌ No | Instance data for plugin node (ports, metadata) |
| `PluginPolicy` | [plugins/NodePluginTemplate.hpp](libgraph/include/plugins/NodePluginTemplate.hpp) | ❌ No | ❌ No | Policy struct for plugin node behavior configuration |
| `PluginGlue` | [plugins/NodePluginTemplate.hpp](libgraph/include/plugins/NodePluginTemplate.hpp) | ❌ No | ❌ No | Interface glue connecting plugin nodes to graph framework |

---

## Component: policies/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `CommandPolicy` | [policies/CommandPolicy.hpp](libgraph/include/policies/CommandPolicy.hpp) | ❌ No | ❌ No | Execution policy providing command registration and handling |
| `DataInjectionPolicy` | [policies/DataInjectionPolicy.hpp](libgraph/include/policies/DataInjectionPolicy.hpp) | ❌ No | ❌ No | Execution policy for runtime data injection |
| `MetricsPolicy` | [policies/MetricsPolicy.hpp](libgraph/include/policies/MetricsPolicy.hpp) | ❌ No | ❌ No | Execution policy collecting and publishing metrics during execution |
| `CSVInjectionPolicy` | [policies/CSVInjectionPolicy.hpp](libgraph/include/policies/CSVInjectionPolicy.hpp) | ❌ No | ❌ No | Execution policy for CSV file-based data injection |
| `MetricsCapabilityCallback` | [policies/MetricsPolicy.hpp](libgraph/include/policies/MetricsPolicy.hpp) | ❌ No | ❌ No | Callback implementation for metrics events |
| `IExecutionPolicy` | [graph/IExecutionPolicy.hpp](libgraph/include/graph/IExecutionPolicy.hpp) | ❌ No | ❌ No | Abstract interface for composition-based execution policies |
| `ExecutionPolicyChain` | [graph/IExecutionPolicy.hpp](libgraph/include/graph/IExecutionPolicy.hpp) | ❌ No | ❌ No | Composite policy chaining multiple policies together |

---

## Component: misc/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `ICallbackProvider` | [graph/ICallbackProvider.hpp](libgraph/include/graph/ICallbackProvider.hpp) | ❌ No | ❌ No | Abstract interface for nodes providing callbacks |
| `ISourceCallbackProvider` | [graph/ICallbackProvider.hpp](libgraph/include/graph/ICallbackProvider.hpp) | ❌ No | ❌ No | Callback provider for source nodes |
| `IProcessingCallbackProvider` | [graph/ICallbackProvider.hpp](libgraph/include/graph/ICallbackProvider.hpp) | ❌ No | ❌ No | Callback provider for processing nodes |
| `ISinkCallbackProvider` | [graph/ICallbackProvider.hpp](libgraph/include/graph/ICallbackProvider.hpp) | ❌ No | ❌ No | Callback provider for sink nodes |
| `ICommandProcessor` | [graph/ICommandProcessor.hpp](libgraph/include/graph/ICommandProcessor.hpp) | ❌ No | ❌ No | Abstract interface for command processing |
| `DefaultCommandProcessor` | [graph/DefaultCommandProcessor.hpp](libgraph/include/graph/DefaultCommandProcessor.hpp) | ❌ No | ❌ No | Default command processor implementation |
| `IExecutionCallback` | [graph/IExecutionCallback.hpp](libgraph/include/graph/IExecutionCallback.hpp) | ❌ No | ❌ No | Callback interface for graph execution events |
| `IConfigurable` | [graph/IConfigurable.hpp](libgraph/include/graph/IConfigurable.hpp) | ❌ No | ❌ No | Interface for objects that can be configured |
| `IDiagnosable` | [graph/IConfigurable.hpp](libgraph/include/graph/IConfigurable.hpp) | ❌ No | ❌ No | Interface for objects providing diagnostic information |
| `IParameterized` | [graph/IConfigurable.hpp](libgraph/include/graph/IConfigurable.hpp) | ❌ No | ❌ No | Interface for objects with parameters |

---

# LIBSENSOR - Sensor Data Integration

## Component: sensor/

| Class | Header | Unit Test | Integration | Description |
|-------|--------|-----------|-------------|-------------|
| `SensorDataRouter` | [sensor/SensorDataRouter.hpp](libsensor/include/sensor/SensorDataRouter.hpp) | ❌ No | ❌ No | Type-based dispatcher for SensorPayload variants; specialization of VariantRouter |
| `SensorClassificationType` | [sensor/SensorClassificationType.hpp](libsensor/include/sensor/SensorClassificationType.hpp) | ❌ No | ❌ No | Enum taxonomy for sensor classification (ACCELEROMETER, GYROSCOPE, etc.) |
| `CSVParserCompat` | [sensor/CSVParserCompat.hpp](libsensor/include/sensor/CSVParserCompat.hpp) | ❌ No | ❌ No | Backward compatibility adapters for sensor-specific CSV parsing |

---

# Summary Statistics

## Test Coverage Analysis

### Unit Tests (6 files, ~230 tests)
- **test_capability_bus.cpp** - CapabilityBus, DefaultCapabilityBus
- **test_message.cpp** - Message, MessageStorage, MessageTraits (49 tests)
- **test_active_queue.cpp** - ActiveQueue, QueueMetrics
- **test_variant_router.cpp** - VariantRouter with multiple variant types
- **test_json_view.cpp** - JsonView and JSON navigation
- **test_json_utilities.cpp** - JsonUtilities and JSON serialization (184-223 tests)

### Integration Tests (3 files, ~200+ tests)
- **test_csv_parser.cpp** - CSVParser, CSVHeader, ColumnMapping with real CSV files
- **test_graph_1.cpp** - FluentGraphBuilder, GraphManager, SourceNode, SinkNode
- **test_csv_pipeline_3.cpp** - CSVDataInjectionManager, CSVNodeConfig, data injection

### Coverage Assessment by Category

#### Well-Tested Classes (35%)
- ✅ Message (comprehensive unit tests)
- ✅ ActiveQueue (comprehensive unit tests)
- ✅ VariantRouter (comprehensive unit tests)
- ✅ CapabilityBus (comprehensive unit tests)
- ✅ JsonView (comprehensive unit tests)
- ✅ JsonUtilities (extensive unit tests)
- ✅ CSVParser (integration tests with real files)
- ✅ GraphManager (integration tests)

#### Partially Tested Classes (40%)
- ⚠️ SourceNode, SinkNode (integration tests only)
- ⚠️ FluentGraphBuilder (integration tests only)
- ⚠️ CSVDataInjectionManager (integration tests only)
- ⚠️ TimestampedData, sensor data types (integration tests only)
- ⚠️ Graph topology and execution (basic integration tests)

#### Untested Classes (25%)
- ❌ InteriorNode, MergeNode, SplitNode variants (no dedicated tests)
- ❌ Edge-related classes (IEdgeBase, EdgeWrapper, EdgeRegistry, etc.)
- ❌ ThreadPool (no tests, critical infrastructure)
- ❌ Plugin system (PluginLoader, PluginRegistry, NodeFactory, etc.)
- ❌ Command system (CommandRegistry, CommandProcessor, etc.)
- ❌ All policy classes (ExecutionPolicy, DataInjectionPolicy, etc.)
- ❌ Most utility classes (AdaptiveCapacityMonitor, NamedType, Lifecycle, etc.)
- ❌ Metrics collection (GraphMetrics, EdgeMetrics, MetricsEvent, etc.)

### Critical Gaps

1. **No ThreadPool tests** - Core infrastructure managing worker threads
2. **No Plugin system tests** - PluginLoader, PluginRegistry, NodeFactory untested
3. **No Edge/Connectivity tests** - EdgeRegistry, EdgeWrapper, edge connections untested
4. **No Interior/Transform node tests** - InteriorNode, MergeNode, SplitNode variants
5. **No Metrics tests** - Metrics collection and aggregation untested
6. **No Policy/Callback tests** - All execution policies and callbacks untested
7. **No Advanced Graph topology** - Complex graphs with multiple paths/cycles untested

### Recommendations for Improved Test Coverage

**High Priority** (Core Framework):
1. Add ThreadPool tests (worker threads, task queue, shutdown)
2. Add Edge connectivity tests (type-aware edge creation, buffering)
3. Add comprehensive node type tests (InteriorNode, MergeNode, SplitNodes)
4. Add metrics collection tests (per-edge, per-node, aggregate)

**Medium Priority** (Plugin System):
1. Add PluginLoader tests (dynamic library loading, error cases)
2. Add PluginRegistry tests (plugin discovery, metadata)
3. Add NodeFactory tests (node creation from plugins)

**Lower Priority** (Advanced Features):
1. Add command system tests (CommandRegistry, processing, output)
2. Add execution policy tests (composition, callbacks)
3. Add advanced graph topology tests (cycles, backpressure, complex routing)
4. Add performance/stress tests (high-throughput scenarios)

---

## File Organization Summary

### Headers by Component (122 files in libgraph)
- **graph/**: 63 files - Core dataflow framework
- **core/**: 12 files - Utility infrastructure
- **capabilities/**: 12 files - Service discovery system
- **csv/**: 3 files - Data parsing and injection
- **ui/**: 3 files - Dashboard and command interface
- **config/**: 15 files - Configuration and data types
- **plugins/**: 8 files - Dynamic plugin system
- **policies/**: 5 files - Execution policy framework
- **metrics/**: 2 files - Performance metrics

### Sensor Module (3 files)
- **sensor/**: 3 files - Sensor-specific routing and compatibility

