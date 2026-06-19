# DSP CPU vs Metal Performance PR2 Implementer Report

## PR

PR2 from `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`: DSP CPU vs Metal Execute-Timing Comparison.

## Files Changed

- `examples/DSP/src/main.cpp`
  - Added `--compare-cpu-metal` mode to the existing DSP runner.
  - Added CPU/GPU config options, plugin-dir option, warm-up and measured iteration controls, executor timeout control, and optional report JSON output.
  - Runs both lanes through `GraphExecutorBuilder`, JSON configs, plugin loading, executor completion, and `GraphExecutor::Execute()`.
  - Records timing only from `ExecutionResult` returned by `Execute()`.
  - Emits an informational JSON report with CPU/GPU iteration timings, summary stats, speedup ratios, correctness/parity fields, selected bins, and Metal availability diagnostics.
  - Marks GPU as unavailable when native Metal is not available instead of fabricating GPU timings.
  - Keeps default comparison mode informational and does not fail only because Metal is slower or unavailable.
- `examples/DSP/CMakeLists.txt`
  - Added the default Metal DSP config path compile definition for the runner.
- `examples/DSP/test/CMakeLists.txt`
  - Added the default Metal DSP config path compile definition for tests.
- `examples/DSP/test/test_dsp_spectrum_demo.cpp`
  - Added comparison report tests.
  - Added equivalent sine-settings test for CPU and GPU configs.
  - Added default informational-mode exit behavior test.

## Files Deleted

- None.

## Tests Added

- `DspCpuVsMetalExecuteTimingTest.WritesInformationalComparisonReport`
- `DspCpuVsMetalExecuteTimingTest.CpuAndGpuConfigsUseEquivalentSineSettings`
- `DspCpuVsMetalExecuteTimingTest.DefaultComparisonModeDoesNotFailForUnavailableOrSlowerMetal`

## Tests Removed

- None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native-strict --target test_dsp_example_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/examples/DSP/test/test_dsp_example_unit --gtest_brief=1
```

Result: passed, 7 tests.

Manual smoke:

```bash
./build-ninja/ninja-debug-metal-native-strict/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native-strict/plugins \
  --warmup-iterations 0 \
  --measured-iterations 1 \
  --executor-timeout-s 8 \
  --report-json /private/tmp/graphx_dsp_cpu_vs_metal_report.json
```

Result: exited 0. CPU lane completed. GPU lane was reported as unavailable in this sandbox with diagnostics: `enumerated_devices=0; default_device=null; likely running in an environment without active GPU access`.

## Scope Guardrails

- No true Metal FFT was implemented.
- No strict performance gate was added.
- No DSP docs or README performance claims were added.
- No external benchmark dependency was added.
- No SAR, GOTCHA, CRSD, MATLAB, SarPy, real data, audio device, or spectrogram image dependency was added.

## Remaining Follow-Up Work

- PR3 can stabilize the report schema and documentation around timing interpretation.
- PR4 can add an explicitly enabled local-only strict speedup gate.
