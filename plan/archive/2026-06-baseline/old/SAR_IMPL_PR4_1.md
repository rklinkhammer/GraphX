# SAR Implementation Report: PR4

Role: `IMPLEMENTER` requested against `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implemented PR4 from `plan/reviews/SAR_PLANNER_REPORT.md`: Put Compatibility Aliases On An Explicit Migration Path.

## Summary

PR4 keeps the existing SAR compatibility aliases in place and makes their boundary explicit. The aliases now have lightweight migration comments, and tests prove they are type aliases to the canonical accel-token implementations rather than separate SAR GPU paths.

No runtime behavior, SAR graph architecture, resolver vocabulary, opaque transport helper semantics, SAR config, build file, or external dependency was changed.

## Files Changed

- `examples/SAR/include/sar/H2DAsyncNode.hpp`
- `examples/SAR/include/sar/D2HAsyncNode.hpp`
- `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`
- `examples/SAR/test/test_sar_token_contract.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`

## Files Deleted

None.

## Tests Added Or Updated

- Added alias-header comments marking the old config-facing names as compatibility aliases and stating the future removal condition: maintained presets and downstream users must migrate to the explicit `*AccelNode` names.
- Added `SarTokenContractTest.CompatibilityAliasesUseCanonicalAccelImplementations`.
- Added `SarTokenContractTest.CompatibilityAliasesDoNotCreateSecondGpuPath`.
- Added `AssertCompatibilityNamesStaySinglePath(...)` to the definitive JSON runtime tests.
- Strengthened definitive config coverage so `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode` resolver variants remain one compatibility path rather than separate `*AccelNode` config-facing paths.

## Verification

- `cmake --build build --target test_sar_example_unit sar_example`
  - Passed.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarTokenContractTest.*:SarJsonRuntimeTest.JsonTopologyRunsWithProviderBootstrapPath:SarJsonRuntimeTest.DefinitivePresetKeepsStrictResolverContractAndPortableIntent:SarJsonRuntimeTest.DefinitivePresetResolvesCommonMetalNodesWithComposedProvider:SarJsonRuntimeTest.ResolverSelectedDeviceStagesPreserveSidecarIdentityAndOpaqueTransportBoundaries'`
  - Passed: 9 tests.

- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 134 tests passed, 1 skipped.
  - Skipped: `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`.
  - Skip reason reported by test: native Metal unavailable because no active/default device was enumerated in this environment.

- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed.
  - Output included successful execution, completion signaled, 9 loaded nodes, and 8 loaded edges.

## Acceptance Notes

- Compatibility aliases remain present.
- Alias behavior is explicitly covered by compile-time and runtime-facing tests.
- Alias input/output token contracts remain `SarAccelControlToken`.
- Config-facing compatibility names remain resolver-visible, while tests assert they do not create a separate `*AccelNode` graph-facing path.
- PR1 `SarAccelControlToken` resolver labels remain intact.
- PR2 centralized helper usage was not changed.
- PR3 sidecar-preservation coverage was not changed.
- No PR5+ work was implemented.
- No external dependencies were added.

## Risks And Follow-Up

- Alias removal is intentionally deferred. Future removal should happen only after maintained presets and downstream users migrate from `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode` to explicit canonical accel implementation names.
- Existing unrelated dirty-tree item remains outside PR4 scope: `plan/prompt examples/cleanup.md`.
