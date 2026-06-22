# SAR Implementation Report: PR2

Role: `IMPLEMENTER` per `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implemented PR2 from `plan/reviews/SAR_PLANNER_REPORT.md`: Centralize Opaque Transport Helper Semantics.

## Files Changed

- `examples/SAR/include/sar/SarRuntimeHelpers.hpp`
- `examples/SAR/src/AzimuthTileSplitNode.cpp`
- `examples/SAR/src/H2DAsyncAccelNode.cpp`
- `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
- `examples/SAR/src/D2HAsyncAccelNode.cpp`
- `examples/SAR/src/ImageTileMergeNode.cpp`
- `examples/SAR/src/SyntheticApertureIqSourceNode.cpp`
- `examples/SAR/src/GotchaReplaySourceNode.cpp`
- `examples/SAR/test/test_sar_runtime_helpers.cpp`

## Files Deleted

- None.

## Changes Made

- Centralized SAR opaque transport helper semantics in `sar::runtime`:
  - `OpaqueHostPointer`
  - `OpaqueReadyEventNotSignaled`
  - `NextOpaqueEventId`
  - `SyntheticDevicePointer`
- Updated SAR nodes to use centralized helpers instead of private duplicate transport helper logic.
- Preserved local token-id counters, because token IDs are not the opaque transport metadata targeted by PR2.
- Preserved sidecar identity behavior through split, H2D, backprojection, D2H, merge, synthetic source, and Gotcha replay source.
- Kept PR1 resolver-contract changes intact.
- Added helper-level test coverage for opaque helper behavior and sidecar invariance.
- Added no external dependencies.

## Tests Added Or Updated

- Updated `SarRuntimeHelpersTest` with coverage for:
  - opaque host pointer sentinel,
  - ready-event sentinel,
  - monotonic opaque event IDs,
  - synthetic device pointer generation,
  - host/device view overload behavior,
  - sidecar identity invariance when transport metadata changes.

## Tests Removed

- None.

## Build And Test Commands

- `cmake --build build --target test_sar_example_unit`
  - Passed.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarRuntimeHelpersTest.*:SarTransportOpaqueContractTest.*:SarAccelNodesTest.*:SarJsonRuntimeTest.*'`
  - Passed: 34 tests.

- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 128 passed, 1 skipped for unavailable native Metal.

- `cmake --build build --target sar_example`
  - Passed.

- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed.

## Acceptance Criteria Status

- Opaque host pointer, synthetic device pointer, and opaque event generation are centralized in SAR runtime helpers.
  - Met.
- SAR nodes use centralized helpers instead of private duplicate helper logic.
  - Met.
- Sidecar identity remains unchanged through H2D, backprojection, D2H, split, and merge.
  - Met by focused and full SAR test runs.
- Transport fields remain documented/tested as opaque transport metadata only.
  - Met by existing opaque transport tests plus new runtime helper tests.
- Existing SAR runtime behavior is preserved.
  - Met.
- PR1 resolver-contract changes remain intact.
  - Met; SAR config scan found no generic `HostPinnedBufferView` / `DeviceBufferView` labels under `examples/SAR/config`.
- No external dependencies are added.
  - Met.

## Remaining Follow-Up

- PR3 still owns deeper resolver/Metal sidecar-preservation coverage.
- Unrelated dirty file remains: `plan/prompt examples/cleanup.md`.
