# JSON Topology Config Test Plan and Node Naming Audit

## Purpose

Create a first-pass implementation plan to duplicate TestGraphTopologies using JSON config files, then verify that node type is not overloaded as node instance name across existing topology builders.

## Context

Graph topology behavior is currently well covered for fluent/topology-builder construction and execution, while JSON config coverage is concentrated on parser and loader validation.

Relevant files:

- libgraph/test/unit/TestGraphTopologies.cpp
- libgraph/test/unit/test_topologies_simple.cpp
- libgraph/test/unit/test_graph_config_parser.cpp
- libgraph/test/unit/test_json_dynamic_graph_loader.cpp
- libgraph/src/graph/GraphBuilder.cpp
- libgraph/src/graph/JsonDynamicGraphLoader.cpp

## Implementation Plan (For Review)

### Phase A: Duplicate the 10 classic topologies from JSON

1. Add a new unit test suite for config-driven topology parity.
2. Build each topology from a JSON file using the production GraphBuilder path.
3. Execute each built graph through the normal executor lifecycle.
4. Assert structural parity (node count, edge count) and runtime parity (completion semantics).

Topologies in scope:

1. SourceOnly
2. MinimalGraph
3. LinearSequential
4. MergeSimple
5. SplitSimple
6. DiamondComplex
7. MultiPathSequential
8. InteriorToMerge
9. ParallelMergeWithInterior
10. ComplexNetwork

### Phase B: Extend parity to producer topologies

1. MinimalIntProducer
2. LinearSequentialIntProducer
3. MinimalDoubleProducer
4. LinearSequentialDoubleProducer

### Test Harness Design

1. Temporary JSON writer helper for each topology case.
2. Config build helper based on GraphBuilder:
   - Create GraphCapability
   - Set node provider
   - Set JSON config path
   - Run GraphBuilder::Build
3. Shared executor helper to run Init/Start/Run/Stop/Join and assert success.
4. Data-driven topology definition table per test case.
5. Shared test-side node factory helper to create named instances in one step.

### Assertions (Minimum)

1. Build success.
2. Node and edge counts match expected topology metadata.
3. Completion behavior matches existing topology tests.

### Assertions (Optional for initial pass)

1. Metrics-event family checks per topology class:
   - produced/consumed
   - split
   - merge
   - transfer

### Validation Gates

1. Focused run for new config-topology tests.
2. Full unit suite.
3. Full integration suite.

## Current Implementation Status

1. The classic topology builders now assign explicit instance names for every node.
2. The producer topology builders now assign explicit instance names for every node.
3. The JSON loader now assigns node names from config `name`, then `id`, instead of leaving the plugin default/type-derived name in place.
4. A shared test helper now centralizes named-node creation so topology builders do not repeat the same `CreateNodeOrThrow` plus `SetName` pattern.
5. Guardrail tests are in place and passing for both topology naming and JSON loader name resolution.
6. The focused validation gates noted above have already been exercised successfully.

## Additional Constraint: Node Type Must Not Be Used As Node Name

### Constraint Statement

- Node type is a class identifier used for dynamic loading and config type resolution.
- Node name is an instance identifier and must be distinct from type identity.

### Verification Method

1. Audited topology builders in TestGraphTopologies.cpp for explicit instance naming.
2. Audited plugin construction defaults in test/plugins/*.cpp.
3. Audited shared plugin wrapper behavior in graph/NodePluginInstance.hpp.
4. Audited runtime loader path in JsonDynamicGraphLoader.cpp for name assignment from config fields.

### Key Findings

1. Default plugin instance naming is initialized from the creation string passed by plugin exports.
2. Test plugin exports pass type strings as that creation string (for example SourceTestNode, SinkTestNode, InteriorTestNode).
3. If topology code does not call SetName, instance name remains type-identical.
4. JsonDynamicGraphLoader now assigns adapter name from node_config.name or node_config.id.

### Topology Compliance Audit

Status key:

- FAIL: at least one node instance in topology can keep type-identical default name.
- PARTIAL: duplicate instances are renamed, but one or more instances still default to type-identical name.

Classic topologies:

1. MinimalGraph: compliant
2. LinearSequential: compliant
3. MergeSimple: compliant
4. SplitSimple: compliant
5. DiamondComplex: compliant
6. MultiPathSequential: compliant
7. InteriorToMerge: compliant
8. ParallelMergeWithInterior: compliant
9. ComplexNetwork: compliant
10. SourceOnly: compliant

Producer topologies:

1. MinimalIntProducer: compliant
2. LinearSequentialIntProducer: compliant
3. MinimalDoubleProducer: compliant
4. LinearSequentialDoubleProducer: compliant

Conclusion:

- The topology suite is now compliant with the node-name constraint.

## Recommended Remediation

### A. Topology builder hardening

1. Completed: explicit instance names are assigned for every node in every topology builder.
2. Completed: the test helper now centralizes instance-oriented naming, for example:
   - source_1, source_2
   - sink_1, sink_2
   - interior_1, interior_2
   - merge_1, split_1, completion_1

### B. JSON loader hardening

1. Completed: JsonDynamicGraphLoader::LoadNodesSafe assigns adapter name during node creation using:
   - node_config.name when present and non-empty
   - otherwise node_config.id
2. Keep node_config.type solely for dynamic loading and type-level behavior.

### C. Guardrail tests

1. Completed: a focused unit test asserts GetName() != GetType() for all built nodes in all topologies.
2. Completed: config-loader tests verify node name resolution order:
   - explicit name field wins
   - fallback to id
   - never fallback to type

## Proposed Execution Order

1. Completed: document-backed parity tests for the 10 classic JSON topologies.
2. Completed: name/type separation assertions in that suite.
3. Completed: topology builder explicit naming updates.
4. Completed: JsonDynamicGraphLoader name assignment from name/id.
5. Completed: parity coverage for producer topologies.
6. Completed: focused validation, full unit suite, and full integration suite.

## Implementation Status (2026-06-05)

1. Topology builders now assign explicit instance names for all nodes in the classic and producer topologies.
2. A shared test helper centralizes named-node creation in the test infrastructure.
3. JsonDynamicGraphLoader now assigns node names from config `name` or `id` instead of leaving the plugin default name in place.
4. Focused validation passed:
   - `cmake --build build -j4`
   - `./libgraph/test/test_libgraph_unit --gtest_filter='TopologiesSimple.*:JsonDynamicGraphLoaderExpectedTest.LoadNodesSafeUsesInjectedDescriptorSchemaProvider'`
5. Guardrail regression tests passed:
   - `TopologiesSimple.TopologyNodesUseInstanceNamesNotTypes`
   - `JsonDynamicGraphLoaderExpectedTest.LoadNodesSafeUsesConfigNameOrIdForNodeNames`
6. The provider-facing test helper was renamed from `GetFactory` to `GetProvider` to match the `INodeProvider`-first orchestration direction.

## Review Questions

1. Should naming convention be strict snake_case instance IDs (source_1) everywhere, including non-JSON builders?
2. Should node_config.name be required for all JSON nodes, or optional with id fallback?
3. Should we fail fast when name equals type in debug/test builds?
