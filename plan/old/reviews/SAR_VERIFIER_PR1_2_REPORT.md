# SAR PR1 Verifier Report (Refreshed)

Role spec: [plan/agents/GRAPHX_SAR_AGENT_ROLES.md](plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

Inputs reviewed:
- [plan/reviews/SAR_INSPECTOR_REPORT.md](plan/reviews/SAR_INSPECTOR_REPORT.md)
- [plan/reviews/SAR_SIMPLIFIER_REPORT.md](plan/reviews/SAR_SIMPLIFIER_REPORT.md)
- [plan/reviews/SAR_PR_ROADMAP.md](plan/reviews/SAR_PR_ROADMAP.md)
- [plan/reviews/SAR_VERIFIER_PR1_1_REPORT.md](plan/reviews/SAR_VERIFIER_PR1_1_REPORT.md)
- Current implemented PR1 workspace state

## Verdict

PASS

## Blocking issues

- None.

## Non-blocking issues

1. `ready_event` remains sequence-derived in runtime accel views/tickets, but SAR identity is now validated as sidecar-driven and invariant to ready_event variation.
- Assignment remains in runtime bookkeeping paths:
  - [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp#L65)
  - [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp#L214)
- Invariance enforcement exists:
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L491)

2. Criterion "Deleted tests were obsolete" remains effectively N/A for this PR slice.
- This implementation primarily updated runtime behavior and added enforcement tests rather than deleting test files.

## Acceptance checks summary

1. No encoded host_ptr identity remains.
- PASS.
- Split node no longer packs identity and now uses opaque token id + opaque host pointer:
  - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L20)
  - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L36)
  - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L56)
- D2H no longer derives host_ptr from sequence identity:
  - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp#L27)
  - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp#L53)
- Host-pointer invariance is now tested explicitly:
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L420)

2. No encoded ready_event identity remains.
- PASS.
- No SAR identity reconstruction from ready_event was found in SAR merge/runtime logic.
- Explicit ready_event invariance test added:
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L491)

3. No global sidecar store remains as primary path.
- PASS.
- No `SarAccelTokenSidecarStore` references found in current SAR code/build wiring.

4. SAR sidecar is carried explicitly.
- PASS.
- Canonical sidecar/token definitions remain explicit:
  - [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L85)
  - [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L104)
  - [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L120)
- End-to-end sidecar preservation test remains in place:
  - [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp#L412)

5. Generic GPU nodes remain SAR-unaware.
- PASS.
- Search across `libgpu` and `libgraph` found no SAR type/header references.

6. Tests cover sidecar preservation.
- PASS.
- Existing sidecar preservation test:
  - [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp#L412)
- Additional PR1 enforcement tests:
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L420)
  - [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L491)

7. Deleted tests were obsolete.
- N/A / Inconclusive for this implementation step (no significant SAR test-file deletions performed).

8. Build and test results are credible.
- PASS.
- Fresh verifier execution results:
  - `Build_CMakeTools`: success (result code 0)
  - `RunCtest_CMakeTools`: 5/5 tests passed (`libgraph_unit`, `libgraph_integration`, `libgpu_stub_unit`, `libgpu_metal_runtime`, `sar_example_unit`)

## Suggested fixes

1. Optional: if you want stricter interpretation of ready_event criterion, remove sequence-derived ready_event assignments and use backend-generated opaque completion events.
2. Optional: add a small negative test to ensure host_ptr changes do not affect any benchmark/trace identity fields beyond raw pointer telemetry display.
