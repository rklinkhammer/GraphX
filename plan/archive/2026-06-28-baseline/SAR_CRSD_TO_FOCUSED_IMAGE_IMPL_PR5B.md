# SAR CRSD To Focused Image IMPLEMENTER Report - PR5b

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: Corrective PR5b from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Real Metal focused-image execution for CRSD-derived phase history

## Scope Implemented

Implemented PR5b by replacing Metal-labeled CPU delegation in `CrsdFocusedImageTransformMetalNode` with a capability-bound native execution lane that performs real allocation, transfer, kernel launch, and readback activity through GraphX Metal capability interfaces.

Completed:

- Added GPU capability binding to the Metal focused-image node (`IGpuCapabilityBinding` implementation).
- Replaced compile-target (`__APPLE__`) availability check with capability/device availability check.
- Implemented native lane execution path with explicit capability activity:
  - Host allocation + population of CRSD-derived phase-history payload
  - Device allocation for input/output
  - H2D transfers via transfer capability
  - Kernel registration/launch via kernel capability
  - D2H transfer into host output buffer
  - Output pixels read from host readback buffer
- Replaced synthetic PR5 diagnostics/tickets with capability-derived values and telemetry-derived dispatch evidence.
- Enforced strict failure conditions in native mode:
  - strict reject when capabilities unavailable and fallback disabled
  - reject on kernel registration/launch/transfer failures
  - reject when `require_kernel_execution=true` and observed kernel dispatch delta is zero
- Preserved explicit CPU fallback semantics:
  - fallback path only used when configured (`allow_fallback=true`)
  - fallback explicitly diagnosed as CPU fallback and not labeled native Metal

## Files Changed

- examples/SAR/include/sar/CrsdFocusedImageTransformMetal.hpp
- examples/SAR/src/CrsdFocusedImageTransformMetal.cpp
- examples/SAR/test/test_crsd_focused_image_metal.cpp

## Native Lane Evidence Added

In `CrsdFocusedImageTransformMetalNode` native path:

- `BindGpuCapabilities(...)` now binds `IMetalContextCapability`, `IMetalMemoryPoolCapability`, `IMetalTransferCapability`, `IMetalKernelCapability`, and `IMetalTelemetryCapability`.
- Native availability now requires bound capabilities.
- Native execution path records nonzero capability activity and maps outputs into sidecar/ticket contracts:
  - `bytes_h2d` from actual H2D ticket byte counts
  - `bytes_d2h` from actual D2H ticket byte count
  - `kernel_dispatches` from telemetry snapshot delta
  - transfer/kernel tickets captured from real transfer/launch calls

## Tests Updated

Updated `CrsdFocusedImageMetalTest` to enforce PR5b proof requirements:

- Added capability binding helper using default Metal capabilities for direct node tests.
- Removed exact CPU-equality assumption in parity test.
- Native lane tests now require observed kernel telemetry and nonzero transfer evidence.
- Added geometry perturbation test proving native output changes with PVP/geometry changes.

Test list after update (8 tests):

1. CpuAndMetalTinyFixtureConfigsExistAndAreWellFormed
2. CpuVsMetalParityUsesSamePhaseHistoryContractAndTolerance
3. MetalDiagnosticsAreNonzeroWhenNativeLaneRuns
4. GuardrailRejectsForwardOnlyMetalExecution
5. PreservesSarAccelControlTokenIdentityWithGpuBackfillDiagnostics
6. NativeMetalOutputChangesWithGeometryPerturbation
7. ResolverDiagnosticsSelectMetalH2DKernelD2HIntents
8. SplitMergePathPreservesSarTokenAndNonzeroGpuDiagnostics

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageMetalTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageTransformNodeTest.*:CrsdFocusedImageMetalTest.*'
```

## Validation Results

- `CrsdFocusedImageMetalTest.*`: 8/8 passed
- `CrsdFocusedImageTransformNodeTest.*:CrsdFocusedImageMetalTest.*`: 24/24 passed

## Constraints Compliance

- No CRSD reader semantics changed.
- CPU focused-image node remains a separate baseline path.
- No SarPy/MATLAB/reference-lane changes added.
- No real-data workflow changes made in this PR5b corrective implementation.

## Remaining Follow-Up Work

- PR5b verifier pass.
