# SAR Implementation Report: PR1

Role: `IMPLEMENTER` per `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implemented PR1 from `plan/reviews/SAR_PLANNER_REPORT.md`: Make SAR Resolver Contracts Explicit.

## Files Changed

- `examples/SAR/config/sar_stripmap_definitive.json`
- SAR config presets under `examples/SAR/config`
- `examples/SAR/test/test_sar_json_runtime.cpp`
- `libgraph/test/unit/test_graph_config_parser.cpp`

## Files Deleted

- None.

## Changes Made

- Replaced SAR resolver mapping token labels from `DeviceBufferView` / `HostPinnedBufferView` to `SarAccelControlToken` across SAR JSON configs.
- Added explicit definitive SAR mappings for:
  - `H2DAsyncNode`
  - `SarBackprojectionTransformNode`
  - `D2HAsyncNode`
- Kept those definitive mappings on the compatibility node names while making the token contract explicit as `SarAccelControlToken`.
- Left generic GPU resolver defaults untouched in `NodeResolutionRegistry`.
- Added parser coverage proving `SarAccelControlToken` resolver mappings are accepted under `edge_contract: accel-token`.
- Updated SAR runtime resolver tests to use the definitive config mappings directly and assert resolver diagnostics carry `SarAccelControlToken`.

## Tests Added Or Updated

- Added `GraphConfigParserExpectedTest.ParseSafeParsesSarAccelTokenResolverMappings`.
- Updated SAR JSON runtime assertions to require `SarAccelControlToken` resolver mapping metadata.
- Updated definitive resolver runtime coverage to use the config-owned H2D/D2H mappings instead of injecting them inside the test.

## Tests Removed

- None.

## Build And Test Commands

- `cmake --build build --target test_sar_example_unit`
  - Passed.
- `cmake --build build --target test_libgraph_unit`
  - Passed.
- `./build/libgraph/test/test_libgraph_unit --gtest_filter='GraphConfigParserExpectedTest.*:ResolvingNodeProviderTest.*'`
  - Passed: 36 tests.
- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarJsonRuntimeTest.*:SarAccelTokenGuardrailsTest.*:SarPr2TokenContractTest.*:SarPr3MetalJsonTest.*'`
  - Passed: 23 tests.
- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 122 passed, 1 skipped for unavailable native Metal.
- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed: exited 0.

## Acceptance Criteria Status

- Definitive SAR config no longer depends on generic view-label vocabulary for SAR accel-token edges.
  - Met.
- Existing SAR runtime behavior is preserved.
  - Met by focused and full SAR unit runs.
- Generic GPU mappings remain available for non-SAR use.
  - Met; `NodeResolutionRegistry` defaults were not changed and resolver-provider tests still pass.
- Legacy SAR payload guardrails still reject obsolete SAR message contracts.
  - Met by parser and SAR guardrail tests.
- Tests cover new SAR token resolver labels and legacy rejection behavior.
  - Met.

## Remaining Follow-Up

- PR2 still owns centralizing duplicated opaque transport helpers.
- PR3 still owns deeper sidecar preservation coverage for resolver/Metal paths.
- The working tree contains unrelated pre-existing plan/report deletions and moves; they were left untouched.
