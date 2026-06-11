# SAR Implementation Report: RRP2

Date: 2026-06-10
PR: RRP2
Title: GraphX Scenario-to-Image Path
Scope: Wire `scenario_001` into the existing GraphX GOTCHA replay plus materialized image path.

## Summary
RRP2 is implemented by extending the scenario-to-GraphX config translation so scenario-driven GOTCHA replay configs now include `SarMaterializedImageSinkNode` in the execution path before merge/diagnostics. A new execution test proves that a config generated from `scenario_001`, when pointed at the existing CI GOTCHA replay fixture, produces a materialized image artifact through the current GraphX runtime path. No SAR math or accel-token architecture was changed.

## 1) Files Changed
- `examples/SAR/tools/rrp1_scenario_to_run.py`
  - Updated `build_graphx_config()` to generate an RRP2 scenario-driven config name.
  - Ensured the source node keeps `emit_watermark=false` for the replay path.
  - Inserted `SarMaterializedImageSinkNode` into the generated topology when absent.
  - Rewired the `d2h -> merge` edge into `d2h -> materialize -> merge`.
  - Enabled materialization when scenario output declares `artifact_kind == "materialized_image"`.

- `examples/SAR/test/CMakeLists.txt`
  - Added `test_rrp2_scenario_image_path.cpp` to `test_sar_example_unit`.

- `examples/SAR/test/test_rrp2_scenario_image_path.cpp`
  - Added an execution test that:
    - generates a scenario-derived config via the RRP1 runner
    - swaps in the existing CI GOTCHA replay fixture
    - verifies the generated topology contains a materialization node and edges
    - executes the GraphX pipeline
    - verifies `SarMaterializedImageSinkNode` captured an image artifact

## 2) Files Deleted
- None.

## 3) Tests Added
- `examples/SAR/test/test_rrp2_scenario_image_path.cpp`
  - `Rrp2ScenarioImagePathTest.ScenarioDrivenConfigCapturesMaterializedImage`

## 4) Tests Removed or Replaced
- None.

## 5) Build Commands Run
- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`

## 6) Test Commands Run
- Focused validation:
  - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Rrp2ScenarioImagePathTest.*'`
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
- RRP3 can now bind the reference boundary to a concrete local gotcha-back invocation and normalized output contract.
- If additional scenario output types are introduced later, `build_graphx_config()` may need to branch on output artifact type instead of always targeting the materialized-image path.
