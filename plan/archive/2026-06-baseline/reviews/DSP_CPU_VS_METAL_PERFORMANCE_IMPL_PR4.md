# DSP CPU vs Metal Performance PR4 Implementer Report

## PR

PR4 from `plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md`: Optional Local Strict Performance Gate.

## Files Changed

- `examples/DSP/src/main.cpp`
  - Added explicitly enabled strict gate controlled by `GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1`.
  - Added optional threshold `GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO`; default is `1.0`.
  - Strict mode uses `run_elapsed_time_ms` from `GraphExecutor::Execute()` returned `ExecutionResult` fields.
  - Strict mode returns non-zero when native Metal is unavailable, GPU lane fails, correctness/parity fails, timing is missing, or measured run-phase speedup is below threshold.
  - Default mode remains informational and exits successfully when GPU is unavailable or not faster.
- `examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json`
  - Added `gate_enforced` report mode.
  - Added `strict_gate` schema fields for enablement, env var names, threshold, timing basis, status, and message.
- `examples/DSP/test/test_dsp_spectrum_demo.cpp`
  - Added guardrail tests proving strict gate is disabled by default.
  - Added strict-mode test for clear failure/reporting when native Metal is unavailable.
  - Extended schema/report assertions for `strict_gate`.
- `docs/dsp/spectrum_demo.md`
  - Documented the local-only strict gate and environment variables.
  - Clarified that the gate uses `run_elapsed_time_ms` from `GraphExecutor::Execute()`.
- `README.md`
  - Added a narrow local-only strict gate example in the DSP section.

## Files Deleted

- None.

## Tests Added

- `DspCpuVsMetalExecuteTimingTest.StrictGateRequiresExplicitEnvironmentOptIn`
- `DspCpuVsMetalExecuteTimingTest.StrictGateFailsClearlyWhenMetalUnavailable`

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

Result: passed, 11 tests.

```bash
python3 -m json.tool examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json
```

Result: passed.

Manual strict-mode smoke:

```bash
env GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1 \
  GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO=1.25 \
  ./build-ninja/ninja-debug-metal-native-strict/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native-strict/plugins \
  --warmup-iterations 0 \
  --measured-iterations 1 \
  --executor-timeout-s 8 \
  --report-json /private/tmp/graphx_dsp_strict_gate_report.json
```

Result: exited `2` in this sandbox because native Metal device enumeration was unavailable. The report contained `mode: gate_enforced`, `strict_gate.enabled: true`, `strict_gate.basis: run_elapsed_time_ms`, and `strict_gate.status: native_metal_unavailable`.

## Scope Guardrails

- Strict gate is local-only and requires `GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1`.
- Default mode remains informational and CI-safe.
- No default CI speedup requirement was added.
- No general Metal/GPU speed claim was added.
- No true Metal FFT was implemented.

## Remaining Follow-Up Work

- PR5 can audit active docs/tests for unqualified performance claims and confirm the strict gate remains opt-in.
