# SAR Implementation Report: RRP1

Date: 2026-06-10
PR: RRP1
Title: Local GOTCHA Reproduction Runner
Scope: Add the smallest local-only harness that consumes `scenario_001` and orchestrates GraphX plus external reference execution boundaries.

## Summary
RRP1 is implemented as a minimal local-only scaffold. It accepts the frozen `scenario_001` manifest, validates it, creates a stable artifact directory layout, generates a GraphX config scaffold from the existing manual GOTCHA topology template, and writes explicit GraphX/reference boundary scripts plus an orchestration plan. It does not download data, clone gotcha-back, run comparators, modify SAR math, or alter accel-token architecture.

## 1) Files Changed
- `examples/SAR/test/CMakeLists.txt`
  - Added `test_rrp1_local_runner.cpp` to `test_sar_example_unit`.
  - Added compile definition for `SAR_RRP1_LOCAL_RUNNER_PATH`.

- `examples/SAR/tools/rrp1_scenario_to_run.py`
  - Added scenario loading/validation helpers.
  - Added scenario-to-GraphX-config translation from `scenario_001` onto the existing manual GOTCHA template.
  - Added artifact-layout and JSON/text write helpers.

- `examples/SAR/tools/rrp1_local_runner.py`
  - Added the local-only RRP1 runner entrypoint.
  - Validates a scenario manifest.
  - Creates a stable output layout.
  - Copies the manifest into the output.
  - Generates `graphx_config.json`.
  - Writes `run_graphx.sh`, `run_gotcha_back.sh`, and `orchestration_plan.json`.

- `examples/SAR/tools/rrp1_local_runner.md`
  - Added runner usage documentation, output layout, scope boundaries, and manual follow-up instructions.

- `examples/SAR/test/test_rrp1_local_runner.cpp`
  - Added local runner scaffold tests for success path and missing-scenario rejection.

## 2) Files Deleted
- None.

## 3) Tests Added
- `examples/SAR/test/test_rrp1_local_runner.cpp`
  - `Rrp1LocalRunnerTest.CreatesExpectedArtifactLayoutFromScenario001`
  - `Rrp1LocalRunnerTest.RejectsMissingScenarioPath`

## 4) Tests Removed or Replaced
- None.

## 5) Build Commands Run
- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`

## 6) Test Commands Run
- Focused validation:
  - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Rrp1LocalRunnerTest.*'`
- Full lane validation:
  - `ctest --test-dir build-ninja/ninja-debug-metal-native --output-on-failure`

Final result:
- `libgraph_unit` passed
- `libgraph_integration` passed
- `libgpu_stub_unit` passed
- `libgpu_metal_runtime` passed
- `sar_example_unit` passed
- 5/5 tests passed

## 7) Remaining Follow-Up Items
- RRP2 can now consume the generated `graphx/graphx_config.json` scaffold and wire scenario-driven GraphX image production.
- RRP3 can later bind the reference boundary script to a concrete local gotcha-back invocation and output normalization path.
