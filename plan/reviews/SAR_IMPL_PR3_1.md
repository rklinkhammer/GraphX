# SAR Implementation Report: PR3

Role: `IMPLEMENTER` requested against `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implemented PR3 from `plan/reviews/SAR_PLANNER_REPORT.md`: Add Sidecar Preservation Coverage For Resolver And Metal Paths.

## Summary

PR3 was implemented as test coverage only. No runtime code, resolver vocabulary, SAR config, external dependency, or compatibility alias behavior was changed.

The added coverage verifies that SAR identity remains in `SarSidecar` across resolver-selected H2D/backprojection/D2H paths and across native/synthetic backprojection paths, while transport fields remain opaque metadata.

## Files Changed

- `examples/SAR/test/test_sar_accel_nodes.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`

## Files Deleted

None.

## Tests Added Or Updated

- Added `ExpectCoreSidecarIdentityEq(...)` helper in `test_sar_accel_nodes.cpp` for identity-field comparisons without freezing timing or transport counters.
- Strengthened native backprojection tests to assert preserved sidecar identity.
- Added `SarAccelNodesTest.H2DSidecarIdentityIsInvariantToHostPointerTransportMetadata`.
- Added `SarAccelNodesTest.BackprojectionSidecarIdentityIsInvariantToDeviceTransportMetadata`.
- Added `SarAccelNodesTest.NativeAndSyntheticBackprojectionPreserveEquivalentSidecarIdentity`.
- Added `SarJsonRuntimeTest.ResolverSelectedDeviceStagesPreserveSidecarIdentityAndOpaqueTransportBoundaries`.

## Verification

- `cmake --build build --target test_sar_example_unit sar_example`
  - Passed.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarAccelNodesTest.*Sidecar*:SarAccelNodesTest.NativeBackprojection*:SarJsonRuntimeTest.JsonTopologyRunsWithProviderBootstrapPath:SarJsonRuntimeTest.ResolverSelectedDeviceStagesPreserveSidecarIdentityAndOpaqueTransportBoundaries:SarJsonRuntimeTest.DefinitivePresetResolvesCommonMetalNodesWithComposedProvider:SarPr3MetalJsonTest.*'`
  - Passed: 21 tests.

- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 132 tests passed, 1 skipped.
  - Skipped: `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`.
  - Skip reason reported by test: native Metal unavailable because no active/default device was enumerated in this environment.

- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed.
  - Output included successful execution, completion signaled, 9 loaded nodes, and 8 loaded edges.

## PR3 Acceptance Notes

- Resolver-selected H2D/backprojection/D2H coverage now asserts definitive resolver diagnostics use `SarAccelControlToken` and no fallback for those stages.
- Runtime sidecar coverage asserts the definitive graph preserves expected final SAR identity while transfer/kernel completion events and `host_ptr` remain non-identity transport metadata.
- Direct accel-node tests assert H2D ignores `host_ptr` for sidecar identity, backprojection ignores `device_ptr` and `ready_event` for sidecar identity, and native/synthetic backprojection preserve equivalent core sidecar identity.
- Native Metal coverage is present through the repository's default Metal capability fixtures. Hardware-dependent native Metal parity remains capability-gated by the existing skipped CPU-reference test.

## Risks And Follow-Up

- The resolver-selected runtime test observes final sidecar state and resolver diagnostics rather than extracting plugin-loaded H2D/D2H node instances, because plugin wrapper typed access is not reliable for all plugin-loaded node handles.
- No PR4 alias migration work was implemented.
- No external SAR dependencies were added.
- Existing unrelated dirty-tree work remains outside PR3 scope, including PR2 changes and `plan/prompt examples/cleanup.md`.
