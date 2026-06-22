# SAR CRSD To Focused Image VERIFIER Report - PR4

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR4 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: Focused image formation path in GraphX

## Verdict

PASS

## Verification Summary

All required PR4 checks passed based on code inspection and test execution.

## Required Checks

1. GraphX computes one real focused SAR image from ordered CRSD-derived full-aperture phase history.
- Result: PASS
- Evidence:
  - Transform implementation performs deterministic backprojection using `sar::reference::BackprojectNearestRange` over assembled full-aperture vectors in `CrsdFocusedImageTransformNode::Transfer`.
  - Full-aperture-only behavior is enforced by rejecting non-EOS data-marker inputs.
  - Test coverage:
    - `CrsdFocusedImageTransformNodeTest.TinyFixturePipelineProducesFiniteNonzeroPeak`
    - `CrsdFocusedImageTransformNodeTest.OneImageProducedFromFullApertureNotPerSegment`

2. Output is not a CRSD signal magnitude quick-look.
- Result: PASS
- Evidence:
  - Transform derives imaging geometry from PVP/geometry fields and performs backprojection, not per-sample magnitude rendering.
  - Test explicitly checks geometry dependence with same signal magnitude but changed platform geometry:
    - `CrsdFocusedImageTransformNodeTest.OutputDependsOnPvpGeometryNotJustMagnitude`

3. Output shape, geometry assumptions, dtype, and layout are explicit.
- Result: PASS
- Evidence:
  - `FocusedImageGrid` explicitly records width/height/pixel spacing/range spacing/wavelength/platform geometry assumptions.
  - Header documentation states assumptions and layout contract.
  - Output payload is `std::vector<float>` (float32), row-major, with size `width * height`.
  - Test coverage:
    - `CrsdFocusedImageTransformNodeTest.OutputGridAndPayloadMetadataAreExplicit`

4. Tests prove data dependence on CRSD signal and PVP/geometry fields.
- Result: PASS
- Evidence:
  - Signal dependence:
    - `CrsdFocusedImageTransformNodeTest.OneSamplePerturbationChangesOutputHash`
  - PVP/geometry dependence:
    - `CrsdFocusedImageTransformNodeTest.PlatformPositionPerturbationChangesOutput`
    - `CrsdFocusedImageTransformNodeTest.OutputDependsOnPvpGeometryNotJustMagnitude`

5. Tests fail for diagnostics-only forwarding, payload-ignored implementation, quick-look output, and one-image-per-segment behavior.
- Result: PASS
- Evidence:
  - Diagnostics-only forwarding guardrail:
    - `CrsdFocusedImageTransformNodeTest.NonEosDataMarkerProducesNullopt`
  - Payload-ignored guardrail:
    - `CrsdFocusedImageTransformNodeTest.EmptyPayloadFrameProducesNullopt`
  - Quick-look guardrail:
    - `CrsdFocusedImageTransformNodeTest.OutputDependsOnPvpGeometryNotJustMagnitude`
  - One-image-per-segment guardrail:
    - `CrsdFocusedImageTransformNodeTest.OneImageProducedFromFullApertureNotPerSegment`

6. Split/merge path is deterministic and preserves SarAccelControlToken.
- Result: PASS
- Evidence:
  - Token preservation test:
    - `CrsdFocusedImageTransformNodeTest.SarAccelControlTokenPreservedInOutput`
  - Deterministic hash repeatability:
    - `CrsdFocusedImageTransformNodeTest.IdenticalInputsProduceDeterministicOutputHash`
  - Boundary hash propagation for partition/split semantics:
    - `CrsdFocusedImageTransformNodeTest.PartitionSchemeHashSurvivesToTransformOutput`

7. Proof matrix covers all-zero, coherent peak, one-sample perturbation, PVP/geometry perturbation, deterministic repeatability, and segment drop/reorder.
- Result: PASS
- Evidence:
  - All-zero:
    - `CrsdFocusedImageTransformNodeTest.AllZeroInputProducesNearZeroImageNotFakeOutput`
  - Coherent peak:
    - `CrsdFocusedImageTransformNodeTest.CoherentMultiSegmentProducesFiniteNonzeroPeak`
  - One-sample perturbation:
    - `CrsdFocusedImageTransformNodeTest.OneSamplePerturbationChangesOutputHash`
  - PVP/geometry perturbation:
    - `CrsdFocusedImageTransformNodeTest.PlatformPositionPerturbationChangesOutput`
  - Deterministic repeatability:
    - `CrsdFocusedImageTransformNodeTest.IdenticalInputsProduceDeterministicOutputHash`
  - Segment drop/reorder behavior:
    - Covered by upstream ordered-set and assembly contract tests in the same unit binary:
      - `CrsdReaderTest.DeterministicDiagnosticsCoverInvalidAndOrderingCases`
      - `CrsdApertureAssemblyAdapterNodeTest.DetectsOutOfOrderAndMissingSegmentDiagnostics`
      - `CrsdApertureAssemblyAdapterNodeTest.DetectsDuplicateAndUnexpectedSegments`

8. No Metal lane, sink artifact contract, SarPy reference generation, local real-data workflow, or MATLAB dependency was added.
- Result: PASS
- Evidence:
  - PR4 diffs add only:
    - focused transform node
    - focused transform plugin target
    - focused transform tests
    - tiny fixture config for source->adapter->transform
  - No new Metal lane/source changes, no new sink artifact node/contract, no SarPy reference generation code, no MATLAB dependency additions.
  - Local real-data workflow remains optional/gated and unchanged in existing test infrastructure.

## Commands Executed

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdFocusedImageTransformNodeTest.*'
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='CrsdApertureAssemblyAdapterNodeTest.*:CrsdReaderTest.*:OrderedCrsdSetInputSourceNodeTest.*'
```

## Test Results

- Focused PR4 suite:
  - 16 passed, 0 failed
- Upstream regression subset:
  - 31 passed, 0 failed, 3 skipped (explicitly gated local real-data smoke)

## Scope Compliance

Verified scope remains PR4-only and does not advance into PR5/PR6/PR7+ concerns.
