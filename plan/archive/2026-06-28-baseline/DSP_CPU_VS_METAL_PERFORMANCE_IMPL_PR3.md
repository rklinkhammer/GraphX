# DSP CPU vs Metal Performance PR3 Implementer Report

## PR

PR3 from `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`: Execute-Timing Report Schema And Statistics Guardrails.

## Files Changed

- `examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json`
  - Added a stable JSON schema for `graphx.dsp.cpu_vs_metal_execute_timing.v1`.
  - Required report fields include config paths, binary path, plugin directories, native Metal diagnostics, warm-up/measured iteration counts, timing source, speedup ratio, correctness summary, and CPU/GPU lane objects.
  - Required per-iteration `execute_result` fields match `ExecutionResult` names:
    - `elapsed_time_ms`
    - `init_elapsed_time_ms`
    - `start_elapsed_time_ms`
    - `run_elapsed_time_ms`
    - `stop_elapsed_time_ms`
    - `join_elapsed_time_ms`
- `examples/DSP/src/main.cpp`
  - Added `build_preset_or_binary_path` and `measurement_context` to CPU-vs-Metal reports.
  - Kept report mode informational.
- `examples/DSP/test/CMakeLists.txt`
  - Added schema path definition for tests.
- `examples/DSP/test/test_dsp_spectrum_demo.cpp`
  - Added schema contract checks.
  - Added deterministic one-iteration summary-statistics checks.
  - Extended report checks for measurement context and binary path.
- `docs/dsp/spectrum_demo.md`
  - Documented CPU-vs-Metal report usage.
  - Documented `elapsed_time_ms` as total `Execute()` wall-clock duration.
  - Documented `run_elapsed_time_ms` as the executor run phase.
  - Documented warm-up exclusion and host/config measurement context.
  - Reiterated that the Metal lane is direct DFT, not GPU FFT.
- `README.md`
  - Added a narrow DSP example/index entry for the informational CPU-vs-Metal execute-timing comparison.

## Files Deleted

- None.

## Tests Added

- `DspCpuVsMetalExecuteTimingTest.ReportMatchesStableSchemaContract`
- `DspCpuVsMetalExecuteTimingTest.StatisticsHandleOneMeasuredIteration`

Existing PR2 tests were also extended to verify:

- `measurement_context`
- `build_preset_or_binary_path`
- schema-aligned timing field names

## Tests Removed

- None.

## Build/Test Commands

```bash
python3 -m json.tool examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json
```

Result: passed.

```bash
cmake --build build-ninja/ninja-debug-metal-native-strict --target test_dsp_example_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/examples/DSP/test/test_dsp_example_unit --gtest_brief=1
```

Result: passed, 9 tests.

## Scope Guardrails

- No strict performance gate was added.
- Report mode remains `informational`; the schema only permits `informational` in this PR.
- No general GPU superiority claim was added.
- Docs do not describe `MetalSpectrumDftNode` as a GPU FFT.
- No true Metal FFT was implemented.

## Remaining Follow-Up Work

- PR4 can add an explicitly enabled local-only strict performance gate and extend the schema/report mode only when that gate exists.
