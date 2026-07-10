# Graph Rev2 Topology and GPU Graph Test Prompt

```text
You are working in the GraphX repository. Address the graph topology and GPU clean-restart findings by making checked-in JSON topology configs the authoritative source of node initialization, then extending that pattern to synthetic GPU graphs in libaccelgraph.

Work autonomously: inspect the repository, implement the changes, build them, run relevant tests, and fix failures. Preserve unrelated user changes. Follow the repository's existing CMake, plugin, GraphBuilder, GraphExecutorBuilder, and JSON topology conventions.

Do not stop after producing a plan. Complete the implementation and verification steps that are feasible on the current host, and clearly report any hardware-specific lanes that must be run on macOS Metal or Jetson CUDA.

## Background

The libgraph JSON topology tests are the reference model:

- checked-in topology configs live under `libgraph/test/config/topologies`;
- `test_json_topology_parity.cpp` iterates the checked-in configs;
- JSON `node_config` is validated against descriptor metadata;
- `JsonDynamicGraphLoader` applies node configuration only through `IConfigurable::Configure`;
- the tests are included in the standard `test_libgraph_unit` target.

The libaccelgraph topology tests currently build graphs through JSON, but several tests then resolve concrete nodes and manually call `Configure` after graph construction. That still uses `IConfigurable`, but bypasses the JSON loader validation/application contract. Bring libaccelgraph into the same discipline as libgraph.

## Phase 1: Make accelgraph JSON configs authoritative

Move all per-node setup currently done by test helpers into `libaccelgraph/test/config/topologies/*.json` as `node_config`.

Cover at least:

- transfer topologies for CPU, Metal, and CUDA;
- spectrum topologies for CPU, Metal, and CUDA;
- strict fallback and fallback-allowed variants where meaningful;
- payload generation fields for transfer ingress nodes;
- spectrum source fields such as sample count, sample rate, tone frequency, amplitude, phase, and packet number;
- spectrum analysis fields such as backend, fallback policy, strict fallback flag, and CUDA device ordinal.

After this phase, topology-style tests must not configure resolved node instances after graph construction.

## Phase 2: Close descriptor/config drift

Update accelgraph node descriptor metadata so every key accepted by `Configure()` is declared by `Fields()` or the repository's equivalent descriptor mechanism.

Known items to inspect and align:

- `SpectrumAnalysisNode` accepts `fallback_policy`;
- `SpectrumAnalysisNode` accepts `cuda_device_ordinal`;
- phase 6b tests previously passed keys such as `complex_sample_count`, `frame_count`, and `output_bins`; either support and declare them intentionally, map them to existing canonical fields, or remove them from topology configs/tests;
- transfer nodes must declare all session and debug-label fields they accept;
- sink nodes that accept only empty config should keep an empty descriptor and should not receive non-empty `node_config`.

Add or update tests that prove:

- valid declared config fields are accepted by the JSON loader;
- unknown `node_config` fields are rejected;
- required fields are enforced;
- wrong field types are rejected.

## Phase 3: Refactor topology tests to use loader-owned initialization

Refactor libaccelgraph topology tests so they:

- build through `GraphExecutorBuilder().WithJsonConfig(...)`;
- use the configured plugin directory;
- execute the graph;
- inspect outputs or diagnostics;
- skip hardware-specific cases with precise diagnostics when native Metal/CUDA is unavailable.

They must not:

- resolve nodes merely to call `Configure`;
- patch runtime parameters onto graph nodes after `GraphExecutorBuilder` has built the graph;
- bypass descriptor validation with direct test-only initialization helpers.

Direct unit tests of individual node classes may still call `Configure` directly when testing the node in isolation. The restriction is for graph/topology tests.

## Phase 4: Add accelgraph JSON topology parity and contract tests

Create a libgraph-style accelgraph test that iterates checked-in accelgraph topology configs and verifies:

- every expected config file exists;
- each config parses;
- graph construction succeeds through the JSON/GraphBuilder path on supported backends;
- expected node and edge counts match a checked-in topology catalog;
- configured nodes expose `IConfigurable`;
- `node_config` is accepted only through declared descriptor fields;
- graph execution succeeds or skips with an exact expected hardware diagnostic.

Keep the test data explicit, reviewable, and close to the existing libgraph `JsonTopologyParityTest` style.

## Phase 5: Add a guard against topology-test configuration bypass

Add a lightweight guard so future topology tests do not regress to manual post-build configuration.

Acceptable approaches:

- a focused test/source scan over `libaccelgraph/test/unit/test_accelgraph_*` topology tests;
- a convention-based helper that centralizes graph construction and makes manual configuration unnecessary;
- a compile-time or review-time guard if it fits existing project patterns.

The guard should distinguish topology tests from direct node unit tests, where direct `Configure` calls are still appropriate.

## Phase 6: Clarify standard test lanes

Ensure these tests are part of standard test registration:

- libgraph topology parity remains in `test_libgraph_unit`;
- libaccelgraph topology/parity tests are included in the standard libaccelgraph test target;
- CUDA-specific tests remain excluded unless `ACCELGRAPH_ENABLE_CUDA` is enabled;
- add discovery coverage similar to `libgraph_unit_discovery` for libaccelgraph so stale or partially configured builds are obvious.

Consider renaming `test_libaccelgraph_smoke` to a clearer unit target only if it can be done without broad churn. Otherwise, keep the name and improve labels/discovery.

## Phase 7: Expand synthetic GPU graph coverage

Using the libgraph topology vocabulary as the model, add synthetic GPU graph configs in small, independently reviewable steps:

- CPU transfer linear: `HostIngress -> HostToDevice -> DeviceToHost -> HostEgress`;
- Metal transfer linear;
- CUDA transfer linear;
- CPU spectrum linear: `SineWaveSource -> SpectrumAnalysis -> SpectrumSink`;
- Metal spectrum linear;
- CUDA spectrum linear;
- fallback-allowed variants;
- strict-fallback variants;
- later fan-out/fan-in graphs once GPU-compatible splitter, merger, or pass-through nodes exist.

GPU kernel nodes do not need complex algorithms. The important coverage is pipeline flow, interconnectivity, plugin loading, config validation, backend selection, lifecycle, and execution diagnostics.

## Phase 8: Fix disconnected or misleading topology nodes

Audit current libaccelgraph topologies for nodes that are present but not exercised. In particular, topologies that include `ReleaseLeaseNode` without wiring it should either:

- wire it into a real release path and assert it executes; or
- remove it from that topology.

Avoid disconnected GPU lifecycle nodes that create false confidence.

## Verification

Use a fresh build/configure when checking test registration so stale binaries do not hide missing tests.

Run the relevant subset locally:

- `test_libgraph_unit --gtest_filter=JsonTopologyParityTest.*`
- `test_libaccelgraph_smoke --gtest_list_tests`
- `ctest --test-dir <fresh-build> -R "libgraph_unit|libaccelgraph_smoke" --output-on-failure`

For hardware-specific lanes:

- run Metal-enabled macOS tests for Metal topologies;
- run CUDA-enabled Jetson tests for CUDA topologies;
- skip with exact, actionable diagnostics when native hardware/runtime support is not available.

## Acceptance criteria

- All topology-style libaccelgraph graph tests use JSON-owned initialization.
- No graph/topology test manually calls `Configure` after graph construction.
- All JSON `node_config` keys are declared by descriptor metadata.
- Unknown, missing-required, and wrong-type config fields are rejected by loader tests.
- Existing libgraph topology parity remains green.
- libaccelgraph topology tests are part of the standard test target and discoverable.
- Synthetic GPU graph coverage exists for transfer and spectrum flows across available CPU, Metal, and CUDA lanes.
- Hardware-specific tests are truthful about native execution, fallback, and skips.

## Final report

Finish with:

- files changed;
- topology configs added or updated;
- descriptor/config metadata changes;
- tests added or refactored;
- build and test commands executed;
- test results and hardware skip reasons;
- remaining follow-up work, especially fan-in/fan-out GPU graph coverage if supporting nodes are not yet available.
```
