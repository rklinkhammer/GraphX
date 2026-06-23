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

### Step 0 (Required First): Create Persistent JSON Topology Config Files

Create checked-in JSON topology configs before adding parity execution tests so reviewers can inspect topology intent directly in version control.

Proposed location:

- `libgraph/test/config/topologies/`

Required files (first-pass review set):

1. `libgraph/test/config/topologies/source_only.json`
2. `libgraph/test/config/topologies/minimal_graph.json`
3. `libgraph/test/config/topologies/linear_sequential.json`
4. `libgraph/test/config/topologies/merge_simple.json`
5. `libgraph/test/config/topologies/split_simple.json`
6. `libgraph/test/config/topologies/diamond_complex.json`
7. `libgraph/test/config/topologies/multi_path_sequential.json`
8. `libgraph/test/config/topologies/interior_to_merge.json`
9. `libgraph/test/config/topologies/parallel_merge_with_interior.json`
10. `libgraph/test/config/topologies/complex_network.json`
11. `libgraph/test/config/topologies/minimal_int_producer.json`
12. `libgraph/test/config/topologies/linear_sequential_int_producer.json`
13. `libgraph/test/config/topologies/minimal_double_producer.json`
14. `libgraph/test/config/topologies/linear_sequential_double_producer.json`

Authoring rules for these files:

1. Use explicit `id` for each node and set `name` for each node instance.
2. Keep `type` strictly for plugin/type lookup.
3. Keep edge wiring explicit and readable (prefer named ports where supported).
4. Keep one topology per file; no embedded multi-topology payloads.
5. Keep these files committed (not temporary runtime artifacts).

### Phase A: Duplicate the 10 classic topologies from JSON

1. Add a new unit test suite for config-driven topology parity.
2. Load each topology from the checked-in JSON files using the production GraphBuilder path.
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

1. Topology-config resolver helper that maps a test case to a checked-in config file path.
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
7. Completed: 14 persistent topology JSON configs were created under `libgraph/test/config/topologies/` as review artifacts.
8. Completed: config-driven parity now supports all 10 classic checked-in JSON topologies (including merge/diamond/interior-to-merge/parallel-merge/complex-network).
9. Completed: producer topologies are now dynamically buildable and executable from checked-in JSON configs.
10. Current parity status: all 14 checked-in JSON topology configs are supported in the config-driven parity suite.

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

1. First step for review: create and commit the 14 persistent topology JSON config files under `libgraph/test/config/topologies/`.
2. Add/adjust config-driven parity tests to consume these checked-in files via GraphBuilder.
3. Add/keep name/type separation assertions in the parity suite.
4. Keep topology builder explicit naming updates in place for fluent-builder parity comparisons.
5. Keep JsonDynamicGraphLoader name assignment from `name`/`id` in place.
6. Extend/confirm parity coverage for producer topologies against the checked-in JSON files.
7. Run focused validation, then full unit suite, then full integration suite.

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
7. Completed first step: persistent topology JSON config files (non-temporary) were added under `libgraph/test/config/topologies/` for reviewer approval before parity test wiring.

## Review Questions

1. Should naming convention be strict snake_case instance IDs (source_1) everywhere, including non-JSON builders?
2. Should node_config.name be required for all JSON nodes, or optional with id fallback?
3. Should we fail fast when name equals type in debug/test builds?
