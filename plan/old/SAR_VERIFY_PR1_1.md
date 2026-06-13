# SAR Verification Report: PR1

Role: `VERIFIER` requested against `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Verified PR1 from `plan/reviews/SAR_PLANNER_REPORT.md`: Make SAR Resolver Contracts Explicit.

## Verdict

PASS.

PR1 satisfies the requested acceptance criteria. No implementation fixes were made during verification.

## Files Inspected

- `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/reviews/SAR_IMPL_PR1_1.md`
- `examples/SAR/config/sar_stripmap_definitive.json`
- SAR JSON presets under `examples/SAR/config`
- `libgraph/src/graph/NodeResolutionRegistry.cpp`
- `libgraph/src/graph/GraphConfigParser.cpp`
- `libgraph/test/unit/test_graph_config_parser.cpp`
- `examples/SAR/test/test_sar_json_runtime.cpp`
- `examples/SAR/include/sar/H2DAsyncNode.hpp`
- `examples/SAR/include/sar/D2HAsyncNode.hpp`
- `examples/SAR/include/sar/SarBackprojectionTransformNode.hpp`
- `examples/SAR/include/sar/SarRuntimeHelpers.hpp`
- SAR accel node/merge/benchmark/main source files checked for PR2+ scope creep by diff.
- Baseline policy/registry and build/dependency manifests checked for external dependency changes by diff.

## Acceptance Criteria Results

1. Definitive SAR config no longer depends on generic view-label vocabulary for SAR accel-token edges.
   - PASS. `sar_stripmap_definitive.json` has explicit `SarAccelControlToken` mappings for `H2DAsyncNode`, `SarBackprojectionTransformNode`, and `D2HAsyncNode`.

2. Existing SAR runtime behavior is preserved.
   - PASS. Focused SAR runtime tests and the full SAR unit binary passed.

3. Generic GPU mappings remain available for non-SAR use.
   - PASS. `NodeResolutionRegistry::CreateDefault()` still uses generic `HostPinnedBufferView` / `DeviceBufferView` contracts for generic GPU mappings, and resolving-provider tests passed.

4. Legacy SAR payload guardrails still reject obsolete SAR message contracts.
   - PASS. Parser rejection paths remain in `GraphConfigParser.cpp`, and parser/SAR guardrail tests passed.

5. Tests cover the new SAR token resolver labels and legacy rejection behavior.
   - PASS. `ParseSafeParsesSarAccelTokenResolverMappings` covers SAR token resolver label acceptance, existing parser/guardrail tests cover legacy rejection, and SAR runtime tests assert `SarAccelControlToken` diagnostics for H2D / backprojection / D2H.

6. Compatibility aliases remain preserved.
   - PASS. `H2DAsyncNode`, `D2HAsyncNode`, and `SarBackprojectionTransformNode` aliases remain in their SAR include headers and still alias the accel-token implementations.

7. No external SAR dependencies were added.
   - PASS. No PR1 diff was observed in baseline registry/policy or dependency/build manifest files.

8. No PR2+ work was implemented.
   - PASS. No PR1 diff was observed in `SarRuntimeHelpers.hpp`, SAR accel node implementations, `ImageTileMergeNode.cpp`, `sar_benchmark.cpp`, or `main.cpp`.

## Tests Run

- `cmake --build build --target test_libgraph_unit test_sar_example_unit`
  - Passed; targets were up to date.

- `./build/libgraph/test/test_libgraph_unit --gtest_filter='GraphConfigParserExpectedTest.*:ResolvingNodeProviderTest.*'`
  - Passed: 36 tests.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarJsonRuntimeTest.*:SarAccelTokenGuardrailsTest.*:SarPr2TokenContractTest.*:SarPr3MetalJsonTest.*'`
  - Passed: 23 tests.

- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 122 passed, 1 skipped.
  - Skipped test: `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`, due to unavailable native Metal device in the environment.

- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed: exited 0.
  - Output remains the existing scaffold message: `Scaffold ready. PR1 node pipeline wiring is pending.`

## Risks And Gaps

- The SAR example executable path runs successfully, but `main.cpp` still reports scaffold output rather than a full runtime/performance report. That is outside PR1 and remains covered by later planner work.
- Generic GPU resolver defaults still use generic view labels by design. SAR configs now use `SarAccelControlToken`.
- The working tree contains unrelated plan/report deletions, moved files under `plan/old`, and other report edits. They were treated as out of scope for PR1 verification.
