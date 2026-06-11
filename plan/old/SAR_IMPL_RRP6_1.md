# SAR Implementation Report: RRP6

Date: 2026-06-10
PR: RRP6
Title: Tiny Deterministic Fixture from Scenario 001
Scope: Derive the CI-safe tiny fixture from the already frozen scenario.

## Summary

RRP6 is implemented by adding a tiny, CI-safe normalized Gotcha fixture that is explicitly traceable to `scenario_001`, plus a focused test that proves the fixture loads through the existing replay path without requiring external-data opt-in. This keeps the fixture small, reproducible, and clearly derived from the frozen scenario rather than introducing a new artifact lineage. No SAR math, accel-token architecture, or replay semantics were changed.

## 1) Files Changed

- `examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json`
  - Added a tiny normalized Gotcha replay fixture.
  - Marked the fixture as `derived_from_scenario: scenario_001`.
  - Marked the fixture as `ci_safe: true`.
  - Reduced the record set to a single traceable record so it stays lightweight for CI use.

- `examples/SAR/test/CMakeLists.txt`
  - Added the RRP6 test file to `test_sar_example_unit`.
  - Added a compile definition for the tiny fixture path.

- `examples/SAR/test/test_rrp6_tiny_fixture.cpp`
  - Added a test that verifies the tiny fixture is traceable to `scenario_001` and remains CI-safe.
  - Added a test that verifies `GotchaReplaySourceNode` accepts the CI-safe tiny fixture without external-data opt-in.

## 2) Files Deleted

- None.

## 3) Tests Added

- `examples/SAR/test/test_rrp6_tiny_fixture.cpp`
  - `Rrp6TinyFixtureTest.TinyFixtureIsTraceableToScenario001AndCiSafe`
  - `Rrp6TinyFixtureTest.GotchaReplaySourceAcceptsCiSafeTinyFixtureWithoutExternalOptIn`

## 4) Tests Removed or Replaced

- None.

## 5) Build Commands Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`

## 6) Test Commands Run

- Focused validation:
  - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Rrp6TinyFixtureTest.*'`

Final result:

- `Rrp6TinyFixtureTest.*` passed.
- The tiny fixture is now explicitly scenario-traceable and verified as CI-safe through the existing replay gate.

## 7) Remaining Follow-Up Items

- None for RRP6 itself. If the frozen scenario evolves later, regenerate the tiny fixture from the new scenario manifest and keep the traceability field updated.