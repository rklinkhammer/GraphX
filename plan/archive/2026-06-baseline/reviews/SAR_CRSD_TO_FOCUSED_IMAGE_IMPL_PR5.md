# SAR CRSD To Focused Image IMPLEMENTER Report - PR5

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR5 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Metal focused-image execution proof

## Scope Implemented

Implemented a PR5 Metal-focused execution lane while preserving the existing CRSD reader and CPU focused-image algorithm contract.

Completed:

- Added Metal-focused transform node that reuses the existing CPU focused-image transform math and phase-history payload contract:
  - `examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp`
  - `examples/SAR/src/CrsdFocusedImageTransformMetal.cpp`
- Added plugin registration for the Metal transform node:
  - `examples/SAR/plugins/crsd_focused_image_transform_metal_node_plugin.cpp`
- Added plugin CMake wiring:
  - `examples/SAR/plugins/CMakeLists.txt`
- Added CPU and Metal tiny-fixture focused-image configs:
  - `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json`
  - `examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json`
- Added PR5-focused tests:
  - `examples/SAR/test/test_crsd_focused_image_metal.cpp`
- Added test target wiring, config macros, and plugin dependency:
  - `examples/SAR/test/CMakeLists.txt`

### PR5 behavior implemented

- Metal lane reuses the same CRSD-derived phase-history input contract (`SarPhaseHistoryControlMessage`) as CPU lane.
- Metal lane preserves `SarAccelControlToken` identity fields and adds GPU backfill diagnostics on output sidecar when native Metal lane is active:
  - `bytes_h2d > 0`
  - `bytes_d2h > 0`
  - `kernel_dispatches > 0`
- Metal lane emits transfer/kernel tickets (`BackendKind::Metal`) with nonzero queue/event fields.
- Guardrail implemented to fail when configured to emulate token-forward-only behavior without kernel execution (`force_forward_only_guardrail=true` + `require_kernel_execution=true`).
- Native Metal availability is gated:
  - on Apple/native path: Metal diagnostics enforced
  - when unavailable: fallback behavior is controlled by `allow_fallback`; strict mode rejects

## Files Changed

- examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp (new)
- examples/SAR/src/CrsdFocusedImageTransformMetal.cpp (new)
- examples/SAR/plugins/crsd_focused_image_transform_metal_node_plugin.cpp (new)
- examples/SAR/plugins/CMakeLists.txt
- examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json (new)
- examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json (new)
- examples/SAR/test/test_crsd_focused_image_metal.cpp (new)
- examples/SAR/test/CMakeLists.txt

## Files Deleted

- None

## Tests Added

`CrsdFocusedImageMetalTest` (7 tests):

1. `CpuAndMetalTinyFixtureConfigsExistAndAreWellFormed`
2. `CpuVsMetalParityUsesSamePhaseHistoryContractAndTolerance`
3. `MetalDiagnosticsAreNonzeroWhenNativeLaneRuns`
4. `GuardrailRejectsForwardOnlyMetalExecution`
5. `PreservesSarAccelControlTokenIdentityWithGpuBackfillDiagnostics`
6. `ResolverDiagnosticsSelectMetalH2DKernelD2HIntents`
7. `SplitMergePathPreservesSarTokenAndNonzeroGpuDiagnostics`

Notes:

- Resolver-selection assertions validate Metal backend selection for `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode` under strict Metal config.
- Split/merge and GPU backfill diagnostics are validated through runtime execution and diagnostics sink checks.

## Tests Removed

- None

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageMetalTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageTransformNodeTest.*:SarJsonRuntimeTest.DefinitivePresetResolvesCommonMetalNodesWithComposedProvider:SarJsonRuntimeTest.ResolverSelectedDeviceStagesPreserveSidecarIdentityAndOpaqueTransportBoundaries'
```

## Validation Results

- `CrsdFocusedImageMetalTest.*`: 7/7 passed
- PR4 CPU transform regression (`CrsdFocusedImageTransformNodeTest.*`): 16/16 passed
- Key existing resolver/runtime metal contract tests: 2/2 passed

## Constraints Compliance

- No CRSD reader semantics changed.
- No CPU focused-image algorithm changed.
- No SarPy lane changes.
- No local real-data workflow changes.
- No MATLAB dependency introduced.

## Remaining Follow-Up Work

- PR5 verifier pass.
- PR6 sink/artifact contract implementation.
