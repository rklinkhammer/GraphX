# SAR PR2 Verifier Report

Role spec: [plan/agents/GRAPHX_SAR_AGENT_ROLES.md](plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

Inputs reviewed:
- [plan/reviews/SAR_INSPECTOR_REPORT.md](plan/reviews/SAR_INSPECTOR_REPORT.md)
- [plan/reviews/SAR_SIMPLIFIER_REPORT.md](plan/reviews/SAR_SIMPLIFIER_REPORT.md)
- [plan/reviews/SAR_PR_ROADMAP.md](plan/reviews/SAR_PR_ROADMAP.md)
- Implemented PR2 diff in current workspace state

## Pass/fail

PASS

## Blocking issues

- None.

## Non-blocking issues

1. Benchmark trace still reports pointer/event telemetry fields (`host_ptr_token`, `ready_event`, `completion_event`) for observability.
- Evidence: [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp#L1110), [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp#L1120), [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp#L1126), [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp#L1132)
- This does not indicate runtime identity transport by itself, but can be misread as such.

2. "Deleted tests were obsolete" remains effectively N/A for PR2 as implemented here.
- PR2 changes update runtime behavior and assertions; no substantial SAR test-file deletions were required in this step.

## Acceptance checks

1. SAR path preserves identity via explicit token/sidecar only.
- PASS.
- Explicit sidecar/token model present: [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L85), [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L104), [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp#L120)
- End-to-end sidecar identity test present: [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp#L412)

2. Sidecar global store removed.
- PASS.
- No `SarAccelTokenSidecarStore` references found in `examples/SAR`.

3. SAR unit suite passes.
- PASS.
- Fresh run: `RunCtest_CMakeTools` passed 5/5 tests, including `sar_example_unit`.

4. No encoded host_ptr identity remains.
- PASS.
- Split and D2H now assign opaque host pointers, not encoded identity:
  - [examples/SAR/src/AzimuthTileSplitNode.cpp](examples/SAR/src/AzimuthTileSplitNode.cpp#L56)
  - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp#L60)
- Host-pointer invariance test present: [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L428)

5. No encoded ready_event identity remains.
- PASS.
- Simulated runtime path sets `ready_event` to `0` and uses opaque completion events:
  - [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp#L72)
  - [examples/SAR/src/H2DAsyncAccelNode.cpp](examples/SAR/src/H2DAsyncAccelNode.cpp#L89)
  - [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp#L219)
  - [examples/SAR/src/SarBackprojectionTransformAccelNode.cpp](examples/SAR/src/SarBackprojectionTransformAccelNode.cpp#L220)
  - [examples/SAR/src/D2HAsyncAccelNode.cpp](examples/SAR/src/D2HAsyncAccelNode.cpp#L85)
  - [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp#L294)
  - [examples/SAR/src/ImageTileMergeNode.cpp](examples/SAR/src/ImageTileMergeNode.cpp#L304)
- Ready-event invariance test present: [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L499)

6. No global sidecar store remains as primary path.
- PASS.

7. SAR sidecar is carried explicitly.
- PASS.

8. Generic GPU nodes remain SAR-unaware.
- PASS.
- Search across `libgpu` and `libgraph` found no SAR token/sidecar references.

9. Tests cover sidecar preservation.
- PASS.
- [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L281)
- [examples/SAR/test/test_sar_json_runtime.cpp](examples/SAR/test/test_sar_json_runtime.cpp#L412)
- Plus PR2 invariance tests at [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L428) and [examples/SAR/test/test_sar_accel_nodes.cpp](examples/SAR/test/test_sar_accel_nodes.cpp#L499)

10. Deleted tests were obsolete.
- N/A / Inconclusive for this implementation slice.

11. Build and test results are credible.
- PASS.
- `Build_CMakeTools`: success.
- `RunCtest_CMakeTools`: final rerun success, 5/5 passed.
- Note: one intermediate run observed a transient `SarPr2FanoutJsonTest` failure before rerun passed; final state is green.

## Suggested fixes

1. Optional clarity cleanup: rename benchmark trace keys to reduce identity-channel ambiguity (`host_ptr_token` -> `host_ptr_observed`, `ready_event` -> `device_ready_event_observed`).
2. Optional stabilization: if `SarPr2FanoutJsonTest` intermittency recurs, tighten deterministic bounds or isolate timing-sensitive assertions to eliminate flake risk.
