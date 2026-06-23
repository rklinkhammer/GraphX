# GRAPHX PR14 Implementer Report: Local-Only SAR Baseline Runner

Status: Complete
Date: 2026-06-23
PR: PR14

## 1. Files changed
- README.md
- plan/BASELINE.md
- examples/SAR/test/CMakeLists.txt
- examples/SAR/test/test_sar_local_baseline_runner.cpp
- examples/SAR/tools/sar_local_baseline_runner.py
- examples/SAR/tools/sar_local_baseline_runner.md

## 2. Files deleted
- None.

## 3. Tests added or updated
- Added: examples/SAR/test/test_sar_local_baseline_runner.cpp
  - `SarLocalBaselineRunnerTest.ProbeDeclaresLocalOnlyBoundary`
  - `SarLocalBaselineRunnerTest.CiSafeSkipWhenOptInNotEnabled`
  - `SarLocalBaselineRunnerTest.ExplicitOptInExercisesLocalSmokePath`
- Updated: examples/SAR/test/CMakeLists.txt
  - Registered `test_sar_local_baseline_runner.cpp` in `test_sar_example_unit`.
  - Added `SAR_LOCAL_BASELINE_RUNNER_PATH` compile definition.

## 4. Tests deleted
- None.

## 5. Build/test commands run
- Build affected target:
  - cd /Users/rklinkhammer/workspace/GraphX/build && ninja test_sar_example_unit
- Run PR14-targeted tests:
  - cd /Users/rklinkhammer/workspace/GraphX/build && ./examples/SAR/test/test_sar_example_unit --gtest_filter="SarLocalBaselineRunnerTest.*:SarBaselineGuardrailTest.*"
  - Result: 7/7 passed

## 6. Acceptance criteria status
- Add a script that invokes the selected package when locally installed: PASS.
  - Added `examples/SAR/tools/sar_local_baseline_runner.py`.
  - Selected baseline package for PR14 runner: SarPy.
  - Runner supports `probe-environment` and `run-local-smoke` commands.
- Gate behind explicit local-only environment/config flags: PASS.
  - Requires `GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1` for smoke attempts.
  - Uses `GRAPHX_SARPY_CRSD_FILE` for local dataset path.
  - Emits deterministic skip diagnostics when opt-in/dependency/data is absent.
- Do not add external packages to normal CI: PASS.
  - No dependency install or CI-default requirement added.
  - Tests are CI-safe and non-blocking in missing-dependency conditions.
- CI passes without external dependency: PASS (validated via targeted unit tests).
- Local runner gives clear missing-dependency diagnostics: PASS.

## 7. Truth-in-labeling status
- Preserved.
- README and BASELINE explicitly label runner as local-only and not a GraphX runtime dependency.
- No production SAR or default-CI external baseline support claims were introduced.

## 8. Remaining follow-up work
- PR15 should add artifact comparison harness wiring to consume outputs produced by the selected local baseline flow.
- If broader local execution is desired, add optional documentation/examples for valid `GRAPHX_SARPY_CRSD_FILE` datasets without changing CI defaults.

## 9. Scope intentionally not touched
- No GraphX runtime/token contract changes.
- No SAR algorithm or graph execution behavior changes.
- No external package integration into build/test dependency chain.
- No PR15 comparison harness or PR16 substitution work implemented.
