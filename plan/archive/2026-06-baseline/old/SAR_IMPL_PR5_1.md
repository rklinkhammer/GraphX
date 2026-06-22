# SAR Implementation Report: PR5

Role: `IMPLEMENTER` requested against `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

Implemented PR5 from `plan/reviews/SAR_PLANNER_REPORT.md`: Split Benchmark And Main Validation Responsibilities.

## Summary

PR5 adds direct test coverage for the `examples/SAR/main.cpp` executable path without changing SAR runtime behavior. `main.cpp` already reported the stable runtime and diagnostic lines required by PR5, so no executable output changes were needed.

Benchmark-only comparison and trace logic remains in `examples/SAR/src/sar_benchmark.cpp`.

## Files Changed

- `examples/SAR/test/CMakeLists.txt`
- `examples/SAR/test/test_sar_main_executable.cpp`

## Files Deleted

None.

## Tests Added Or Updated

- Added `SarMainExecutableTest.DefinitiveConfigReportsRuntimeAndDiagnostics`.
  - Runs the built `sar_example` executable.
  - Uses `sar_stripmap_definitive.json`.
  - Uses the SAR plugin output directory from the current build.
  - Captures stdout/stderr.
  - Asserts stable output for:
    - executable start banner,
    - topology config,
    - plugin directory,
    - loaded node count,
    - loaded edge count,
    - successful execution,
    - completion signal,
    - diagnostics queue backpressure metric,
    - diagnostics peak queue depth metric.
- Added `SAR_EXAMPLE_EXECUTABLE_PATH` compile definition for the SAR unit test binary.
- Added `test_sar_example_unit` dependency on `sar_example`.
- Added CTest entry `sar_example_main_executable`.

## Verification

- `cmake --build build --target test_sar_example_unit sar_example`
  - Passed.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarMainExecutableTest.*'`
  - Passed: 1 test.

- `ctest --test-dir build -R '^sar_example_main_executable$' --output-on-failure`
  - Passed: 1 CTest entry.

- `./build/examples/SAR/test/test_sar_example_unit --gtest_filter='SarMainExecutableTest.*:SarJsonRuntimeTest.*:SarTokenContractTest.*'`
  - Passed: 16 tests.

- `./build/examples/SAR/test/test_sar_example_unit`
  - Passed: 135 tests passed, 1 skipped.
  - Skipped: `SarCpuReferenceTest.BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable`.
  - Skip reason reported by test: native Metal unavailable because no active/default device was enumerated in this environment.

- `./build/examples/SAR/sar_example examples/SAR/config/sar_stripmap_definitive.json build/examples/SAR/plugins`
  - Passed.
  - Output included successful execution, completion signaled, 9 loaded nodes, 8 loaded edges, and diagnostics metrics.

## Output And Benchmark Boundary

- No output fields were added to `examples/SAR/src/main.cpp`.
- Existing main output already includes:
  - `Diagnostics queue_backpressure_events`
  - `Diagnostics peak_queue_depth`
- `examples/SAR/src/sar_benchmark.cpp` was not changed. Benchmark-only graph-vs-direct comparison and trace logic stayed there.

## Acceptance Notes

- `examples/SAR/main.cpp` executable path is now directly test-covered.
- Basic runtime diagnostics from available graph/diagnostics data are asserted.
- Existing SAR runtime behavior is preserved.
- PR1 resolver labels remain intact.
- PR2 centralized helper semantics remain intact.
- PR3 sidecar-preservation tests remain intact.
- PR4 compatibility alias migration path remains intact.
- No external dependencies were added.
- No PR6+ work was implemented.

## Risks And Follow-Up

- The subprocess test asserts stable text labels rather than exact diagnostic values, because queue depth can vary by scheduler timing.
- Existing unrelated dirty-tree item remains outside PR5 scope: `plan/prompt examples/cleanup.md`.
