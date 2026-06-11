# SAR Verification Report: PR2

Role: `VERIFIER` requested against `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Verified PR2 from `plan/reviews/SAR_PLANNER_REPORT.md`: Centralize Opaque Transport Helper Semantics.

## Verdict

PASS.

PR2 satisfies the requested acceptance criteria. No implementation fixes were made during verification.

## Files Inspected

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_VERIFY_PR1_1.md`
- `plan/reviews/SAR_IMPL_PR2_1.md`
- `examples/SAR/include/sar/SarRuntimeHelpers.hpp`
- `examples/SAR/src/AzimuthTileSplitNode.cpp`
- `examples/SAR/src/H2DAsyncAccelNode.cpp`
- `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`
- `examples/SAR/src/D2HAsyncAccelNode.cpp`
- `examples/SAR/src/ImageTileMergeNode.cpp`
- `examples/SAR/src/SyntheticApertureIqSourceNode.cpp`
- `examples/SAR/src/GotchaReplaySourceNode.cpp`
- `examples/SAR/test/test_sar_runtime_helpers.cpp`
- `examples/SAR/test/test_sar_transport_opaque_contract.cpp`
- `examples/SAR/config`
- `libgraph/src/graph/NodeResolutionRegistry.cpp`
- Build/dependency and external baseline files by diff.

## Acceptance Criteria Results

1. Opaque host pointer generation is centralized in SAR runtime helpers.
   - PASS. `sar::runtime::OpaqueHostPointer()` is defined in `SarRuntimeHelpers.hpp`; SAR node call sites use `runtime::OpaqueHostPointer()`.

2. Synthetic device pointer generation is centralized in SAR runtime helpers.
   - PASS. `sar::runtime::SyntheticDevicePointer(...)` overloads are defined in `SarRuntimeHelpers.hpp`; H2D and backprojection use those helpers.

3. Opaque event ID generation is centralized in SAR runtime helpers.
   - PASS. `sar::runtime::NextOpaqueEventId()` is defined in `SarRuntimeHelpers.hpp`; H2D, D2H, backprojection, and merge use it.

4. SAR nodes use centralized helpers instead of private duplicate helper logic.
   - PASS. Search found no private duplicate `OpaqueHostPointer`, `MakeSyntheticDevicePointer`, or local `NextOpaqueEventId` implementations outside `SarRuntimeHelpers.hpp`.

5. Sidecar identity remains unchanged through split, H2D, backprojection, D2H, and merge.
   - PASS. Existing accel-node and opaque-transport tests passed, and new helper tests cover sidecar invariance when transport metadata changes.

6. Transport fields remain documented/tested as opaque transport metadata only.
   - PASS. `SarMessages.hpp` still documents transport opacity; `SarTransportOpaqueContractTest` and `SarRuntimeHelpersTest` passed.

7. Existing SAR runtime behavior is preserved.
   - PASS. Focused SAR tests, full SAR unit suite, and `sar_example` executable path passed.

8. PR1 resolver-contract changes remain intact.
   - PASS. Search found no generic `HostPinnedBufferView` / `DeviceBufferView` labels under `examples/SAR/config`; generic GPU defaults remain in `NodeResolutionRegistry`.

9. No external SAR dependencies were added.
   - PASS. No PR2 diff was observed in baseline policy/registry or build/dependency manifest files.

10. No PR3+ work was implemented.
   - PASS. Token-id sequencing remains local; resolver/Metal sidecar-preservation work was not expanded beyond existing tests.

## Tests Run

- `cmake --build build --target test_sar_example_unit sar_example`
  - Passed; targets were up to date.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarRuntimeHelpersTest.*:SarTransportOpaqueContractTest.*:SarAccelNodesTest.*:SarJsonRuntimeTest.*'`
  - Passed: 34 tests.

- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 128 passed, 1 skipped.
  - Skipped test: `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`, due to unavailable native Metal device in the environment.

- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed: exited 0.
  - Output included successful graph execution and completion signal.

## Risks And Gaps

- `NextOpaqueEventId()` is now one shared monotonic helper across SAR helper users. This is consistent with PR2 centralization and tests passed, but any future test that assumes per-node event sequences would need to treat event IDs as opaque.
- Token ID sequencing remains node-local by design because PR2 targets transport metadata, not token identity generation.
- Unrelated dirty working-tree item remains: `plan/prompt examples/cleanup.md`.
