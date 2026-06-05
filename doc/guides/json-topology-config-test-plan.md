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

## Additional Constraint: Node Type Must Not Be Used As Node Name

### Constraint Statement
- Node type is a class identifier used for dynamic loading and config type resolution.
- Node name is an instance identifier and must be distinct from type identity.

### Verification Method
1. Audited topology builders in TestGraphTopologies.cpp for explicit SetName usage.
2. Audited plugin construction defaults in test/plugins/*.cpp.
3. Audited shared plugin wrapper behavior in graph/NodePluginInstance.hpp.
4. Audited runtime loader path in JsonDynamicGraphLoader.cpp for name assignment from config fields.

### Key Findings
1. Default plugin instance naming is initialized from the creation string passed by plugin exports.
2. Test plugin exports pass type strings as that creation string (for example SourceTestNode, SinkTestNode, InteriorTestNode).
3. If topology code does not call SetName, instance name remains type-identical.
4. JsonDynamicGraphLoader currently does not assign adapter name from node_config.id or node_config.name.

### Topology Compliance Audit
Status key:
- FAIL: at least one node instance in topology can keep type-identical default name.
- PARTIAL: duplicate instances are renamed, but one or more instances still default to type-identical name.

Classic topologies:
1. MinimalGraph: FAIL
2. LinearSequential: FAIL
3. MergeSimple: PARTIAL
4. SplitSimple: PARTIAL
5. DiamondComplex: PARTIAL
6. MultiPathSequential: PARTIAL
7. InteriorToMerge: PARTIAL
8. ParallelMergeWithInterior: PARTIAL
9. ComplexNetwork: PARTIAL
10. SourceOnly: FAIL

Producer topologies:
1. MinimalIntProducer: FAIL
2. LinearSequentialIntProducer: FAIL
3. MinimalDoubleProducer: FAIL
4. LinearSequentialDoubleProducer: FAIL

Conclusion:
- No topology is fully compliant with the node-name constraint today.

## Recommended Remediation

### A. Topology builder hardening
1. Assign explicit instance names for every node in every topology builder, not only duplicates.
2. Use instance-oriented naming scheme, for example:
   - source_1, source_2
   - sink_1, sink_2
   - interior_1, interior_2
   - merge_1, split_1, completion_1

### B. JSON loader hardening
1. In JsonDynamicGraphLoader::LoadNodesSafe, assign adapter name during node creation using:
   - node_config.name when present and non-empty
   - otherwise node_config.id
2. Keep node_config.type solely for dynamic loading and type-level behavior.

### C. Guardrail tests
1. Add a focused unit test that asserts GetName() != GetType() for all built nodes in all topologies.
2. Add config-loader tests that verify node name resolution order:
   - explicit name field wins
   - fallback to id
   - never fallback to type

## Proposed Execution Order
1. Add document-backed parity tests for the 10 classic JSON topologies.
2. Add name/type separation assertions to that suite.
3. Implement topology builder explicit naming updates.
4. Implement JsonDynamicGraphLoader name assignment from name/id.
5. Extend parity suite to producer topologies.
6. Run full validation gates.

## Implementation Status (2026-06-05)
1. Topology builders now assign explicit instance names for all nodes in the classic and producer topologies.
2. JsonDynamicGraphLoader now assigns node names from config `name` or `id` instead of leaving the plugin default name in place.
3. Focused validation passed:
   - `cmake --build build -j4`
   - `./libgraph/test/test_libgraph_unit --gtest_filter='TopologiesSimple.*:JsonDynamicGraphLoaderExpectedTest.LoadNodesSafeUsesInjectedDescriptorSchemaProvider'`
4. Guardrail regression tests passed:
   - `TopologiesSimple.TopologyNodesUseInstanceNamesNotTypes`
   - `JsonDynamicGraphLoaderExpectedTest.LoadNodesSafeUsesConfigNameOrIdForNodeNames`

## Review Questions
1. Should naming convention be strict snake_case instance IDs (source_1) everywhere, including non-JSON builders?
2. Should node_config.name be required for all JSON nodes, or optional with id fallback?
3. Should we fail fast when name equals type in debug/test builds?
