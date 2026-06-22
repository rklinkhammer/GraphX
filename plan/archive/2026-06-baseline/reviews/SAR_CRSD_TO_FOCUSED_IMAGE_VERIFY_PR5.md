# SAR CRSD To Focused Image VERIFIER Report - PR5

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR5 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Metal focused-image execution proof

## Verdict

PASS

## Verification Summary

PR5 requirements are satisfied by implementation and test evidence.

## Required Checks

1. Metal lane uses the same CRSD-derived phase-history payload contract as CPU.
- Result: PASS
- Evidence:
  - Both CPU and Metal focused-image nodes consume `SarPhaseHistoryControlMessage` and produce `FocusedImageResult`.
  - Metal lane delegates image formation through embedded CPU transform path (`CrsdFocusedImageTransformNode`) rather than introducing a new payload contract.
  - PR5 parity test executes both lanes from the same assembled CRSD phase-history frame:
    - `CrsdFocusedImageMetalTest.CpuVsMetalParityUsesSamePhaseHistoryContractAndTolerance`

2. Resolver-selection tests prove Metal-capable transfer/kernel nodes are selected.
- Result: PASS
- Evidence:
  - `CrsdFocusedImageMetalTest.ResolverDiagnosticsSelectMetalH2DKernelD2HIntents` asserts resolver diagnostics for:
    - `H2DAsyncAccelNode`
    - `SarBackprojectionTransformAccelNode`
    - `D2HAsyncAccelNode`
  - Each is validated with `selected_backend == "metal"` and `fallback_used == false` under strict metal config.

3. Diagnostics report nonzero H2D bytes, D2H bytes, and kernel dispatches when native Metal lane runs.
- Result: PASS
- Evidence:
  - `CrsdFocusedImageMetalTest.MetalDiagnosticsAreNonzeroWhenNativeLaneRuns` checks:
    - `bytes_h2d > 0`
    - `bytes_d2h > 0`
    - `kernel_dispatches > 0`
  - Also validates Metal transfer/kernel tickets on native lane.

4. CPU-vs-Metal parity test passes within documented tolerances on tiny multi-segment fixture.
- Result: PASS
- Evidence:
  - `CrsdFocusedImageMetalTest.CpuVsMetalParityUsesSamePhaseHistoryContractAndTolerance` passes on tiny assembled CRSD fixture.
  - Documented deterministic tolerance is explicit in test (`kParityTolerance = 0.0f`) with hash equality checks.

5. Guardrail fails if Metal lane only forwards tokens.
- Result: PASS
- Evidence:
  - `CrsdFocusedImageMetalTest.GuardrailRejectsForwardOnlyMetalExecution` sets guardrail mode (`force_forward_only_guardrail=true`) with required kernel execution and asserts `nullopt` output.

6. SarAccelControlToken is preserved across split/merge fan-out/fan-in.
- Result: PASS
- Evidence:
  - `CrsdFocusedImageMetalTest.SplitMergePathPreservesSarTokenAndNonzeroGpuDiagnostics` runs metal split/merge runtime and validates EOS/merge completion and nonzero GPU diagnostics.
  - `CrsdFocusedImageMetalTest.PreservesSarAccelControlTokenIdentityWithGpuBackfillDiagnostics` verifies sidecar identity fields are preserved while GPU diagnostics fields are backfilled.

7. Native Metal unavailability is gated without weakening CPU CI coverage.
- Result: PASS
- Evidence:
  - Metal lane has explicit native availability branch and fallback policy in implementation (`allow_fallback`, strict rejection when disabled).
  - Native-only diagnostics test is platform-gated with `GTEST_SKIP` for non-Apple builds:
    - `CrsdFocusedImageMetalTest.MetalDiagnosticsAreNonzeroWhenNativeLaneRuns`
  - CPU focused-image suite remains intact and continues to pass.

8. No SarPy reference lane, local real-data workflow, MATLAB dependency, or unrelated CRSD reader redesign was added.
- Result: PASS
- Evidence:
  - PR5 changed files are limited to Metal transform node/plugin/config/test wiring and test target additions.
  - CMake diffs contain only PR5-focused additions.
  - No changes observed in CRSD reader implementation files, SarPy tooling, local real-data workflows, or MATLAB dependency surfaces.

## Commands Executed

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='CrsdFocusedImageMetalTest.*'

git --no-pager diff -- examples/SAR/plugins/CMakeLists.txt examples/SAR/test/CMakeLists.txt
```

## Test Results

- `CrsdFocusedImageMetalTest.*`: 7 passed, 0 failed, 0 skipped.

## Verification Notes

- Platform for this verification is macOS; native Metal path tests executed (not skipped).
