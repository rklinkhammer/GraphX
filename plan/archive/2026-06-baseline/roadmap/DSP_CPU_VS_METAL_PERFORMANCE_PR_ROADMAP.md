# DSP CPU vs Metal Performance PR Roadmap

## Current Timing Model

The current `GraphExecutor` lifecycle exposes timing values through existing
result objects:

- `InitializationResult::elapsed_time_ms` from `GraphExecutor::Init()` and
  `InitExpected()`: graph policy initialization plus graph/node initialization.
- `ExecutionResult::elapsed_time_ms` from `Start()` and `StartExpected()`:
  policy start plus graph/node start.
- `ExecutionResult::elapsed_time_ms` from `Run()` and `RunExpected()`:
  blocking runtime wait from `Run()` entry until the graph capability reports
  stopped. This includes completion-policy wait/shutdown latency and is the
  closest existing field for graph execution duration.
- `ExecutionResult::elapsed_time_ms` from `Stop()` and `StopExpected()`:
  stop signaling, graph stop, and policy stop.
- `ExecutionResult::elapsed_time_ms` from `Join()` and `JoinExpected()`:
  node thread join plus policy join.
- `GraphExecutor::Execute()` calls the lifecycle methods but currently returns
  a fresh `ExecutionResult` without consolidated timing.

Planner conclusion: fix `GraphExecutor::Execute()` first. CPU-vs-Metal
comparison should use the runtime's own consolidated `Execute()` timing fields,
not an invented benchmark-local lifecycle harness. The benchmark/reporting layer
can repeat `Execute()` for warm-up and measured iterations, but timing must come
from the `ExecutionResult` returned by `Execute()`.

## PR1: Consolidated GraphExecutor Execute Timing

**Purpose**

Make `GraphExecutor::Execute()` the canonical source for end-to-end executor
timing by preserving per-phase lifecycle timings and total execute timing in the
returned `ExecutionResult`.

**Files to touch**

- `libgraph/include/graph/ExecutionResult.hpp`
- `libgraph/include/graph/GraphExecutor.hpp` only if comments/contracts need
  updating.
- `libgraph/src/graph/GraphExecutor.cpp`
- `libgraph/test/unit/test_graph_executor_timing_contract.cpp`
- `libgraph/test/CMakeLists.txt` only if the existing glob does not pick up the
  new test file.

**Files to delete**

- None.

**Tests to add**

- `GraphExecutorTimingContractTest.ExecuteReturnsTotalElapsedTime`
- `GraphExecutorTimingContractTest.ExecuteReturnsLifecyclePhaseTimings`
- `GraphExecutorTimingContractTest.ExecuteTimingFailureReportsCompletedPhases`
- `GraphExecutorTimingContractTest.ManualLifecycleTimingStillWorks`

**Tests to delete**

- None.

**Acceptance criteria**

- `ExecutionResult` gains explicit consolidated timing fields for `Execute()`,
  such as:
  - `init_elapsed_time_ms`
  - `start_elapsed_time_ms`
  - `run_elapsed_time_ms`
  - `stop_elapsed_time_ms`
  - `join_elapsed_time_ms`
  - `elapsed_time_ms` as total `Execute()` wall-clock duration.
- `GraphExecutor::ExecuteExpected()` measures total wall-clock duration around
  the full lifecycle call sequence.
- `ExecuteExpected()` copies lifecycle result timing fields into the returned
  `ExecutionResult`.
- If a lifecycle phase fails, the returned/failure-facing result preserves any
  completed phase timings where the existing error path allows it, and the
  failure message identifies the failed phase.
- Existing `Init`, `Start`, `Run`, `Stop`, and `Join` return contracts remain
  compatible.
- No benchmark executable, DSP docs, or performance claims are added in this PR.

**Risks**

- `ExecutionResult` is a public graph result type. Adding fields is acceptable,
  but changing existing field semantics is not.
- Millisecond granularity may be coarse for small DSP graphs. This PR should
  expose the current timing truth, not invent high-resolution timing elsewhere.
- Failure paths may not currently carry partial `ExecutionResult` objects
  through `std::expected`; tests should define the best behavior possible
  without redesigning error transport.

**Rollback plan**

Remove the new `ExecutionResult` timing fields and restore `ExecuteExpected()`
to returning only success/message/state. Later benchmark PRs should not proceed
without another canonical runtime timing source.

**CI-safe or local-only**

CI-safe.

## PR2: DSP CPU vs Metal Execute-Timing Comparison

**Purpose**

Add CPU-vs-Metal comparison using the existing CPU DSP config and Metal DSP
config, driven by repeated `GraphExecutor::Execute()` calls and the consolidated
timing fields added in PR1.

**Files to touch**

- `examples/DSP/src/main.cpp` if the existing runner can naturally accept a
  compare mode without muddying the CPU demo.
- Or `examples/DSP/src/dsp_cpu_vs_metal_benchmark.cpp` if a separate executable
  is cleaner.
- `examples/DSP/CMakeLists.txt`
- `examples/DSP/test/test_dsp_cpu_vs_metal_benchmark.cpp`
- `examples/DSP/test/CMakeLists.txt`
- `libdsp/config/dsp_sine_fft_spectrum_256.json`
- `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`

**Files to delete**

- None.

**Tests to add**

- `DspCpuVsMetalBenchmarkTest.UsesExecuteResultTimingFields`
- `DspCpuVsMetalBenchmarkTest.RunsCpuAndGpuConfigsWithEquivalentSineSettings`
- `DspCpuVsMetalBenchmarkTest.WarmupIterationsAreExcludedFromMeasuredSummary`
- `DspCpuVsMetalBenchmarkTest.MetalUnavailableReportsUnavailableClearly`
- `DspCpuVsMetalBenchmarkTest.DefaultModeDoesNotFailWhenMetalIsSlower`

**Tests to delete**

- None.

**Acceptance criteria**

- The comparison runner uses `GraphExecutorBuilder`, JSON configs, plugin
  loading, executor completion, and `GraphExecutor::Execute()` result timing.
- It does not manually time lifecycle phases as the primary timing source.
- Inputs include:
  - `--cpu-config`
  - `--gpu-config`
  - `--plugin-dir`
  - optional `--additional-plugin-dir`
  - `--warmup-iterations`
  - `--measured-iterations`
  - `--executor-timeout-s`
  - `--report-json`
- Defaults use:
  - `libdsp/config/dsp_sine_fft_spectrum_256.json`
  - `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`
  - shared build plugin directory.
- CPU and GPU lanes use deterministic equivalent sine settings:
  `-1000 Hz`, amplitude `1.0`, sample rate `48000 Hz`, packet size `256`.
- Warm-up iterations run before measured iterations and are excluded from
  summary statistics.
- Measured iterations record `ExecutionResult` fields from `Execute()`:
  total `elapsed_time_ms`, `init_elapsed_time_ms`, `start_elapsed_time_ms`,
  `run_elapsed_time_ms`, `stop_elapsed_time_ms`, and `join_elapsed_time_ms`.
- Report includes min, median, mean, standard deviation, and speedup ratio for
  total execute time and run phase time.
- Report includes correctness/parity summary using existing peak frequency,
  peak magnitude, and selected-bin comparison logic or a shared helper derived
  from PR6 parity tests.
- If native Metal is unavailable, the report marks GPU status unavailable and
  does not fabricate GPU timing.
- Default exit status is informational: “Metal not faster” is a valid result,
  not a failure.
- No true Metal FFT is implemented.

**Risks**

- `N=256` direct DFT may be too small for stable performance conclusions.
  Report text must say “measured on this host/config” and include iteration
  counts.
- Plugin loading, graph construction, and Metal kernel setup can dominate first
  iterations. Warm-up exists to reduce that noise.
- A separate executable avoids overloading the CPU demo runner, but another
  binary adds CMake/test surface. Choose the smallest option that keeps
  user-facing behavior clear.

**Rollback plan**

Remove the comparison runner, tests, and CMake wiring. Keep PR1 consolidated
`Execute()` timing because it is generally useful runtime instrumentation.

**CI-safe or local-only**

CI-safe by default because it must skip/report Metal unavailable and must not
fail only because Metal is slower.

## PR3: Execute-Timing Report Schema And Statistics Guardrails

**Purpose**

Make the comparison artifact stable and machine-readable using the consolidated
`ExecutionResult` fields from `Execute()`.

**Files to touch**

- `examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json`
- `examples/DSP/test/test_dsp_cpu_vs_metal_benchmark.cpp`
- `docs/dsp/spectrum_demo.md`
- `README.md` only in the existing DSP example/index section.

**Files to delete**

- None.

**Tests to add**

- `DspCpuVsMetalBenchmarkTest.ReportMatchesSchema`
- `DspCpuVsMetalBenchmarkTest.ReportUsesExecuteTimingFields`
- `DspCpuVsMetalBenchmarkTest.StatisticsHandleOneAndManyIterations`
- `DspCpuVsMetalBenchmarkTest.SpeedupRatioIsInformationalByDefault`
- `DspCpuVsMetalBenchmarkTest.ReportLabelsMeasuredOnHostAndConfig`

**Tests to delete**

- None.

**Acceptance criteria**

- JSON schema requires:
  - `schema`
  - `cpu_config_path`
  - `gpu_config_path`
  - `build_preset_or_binary_path`
  - `plugin_directories`
  - `native_metal_available`
  - `native_metal_diagnostics`
  - `warmup_iterations`
  - `measured_iterations`
  - per-iteration `execute_result` timing fields
  - summary statistics for CPU and GPU
  - `speedup_ratio`
  - `correctness_summary`
  - `mode`: `informational` or `gate_enforced`
- Report field names match `ExecutionResult` timing names instead of inventing
  parallel benchmark timing names.
- Statistics use deterministic calculations and stable numeric field names.
- Default report mode is `informational`.
- Docs explain:
  - `elapsed_time_ms` is total `Execute()` wall-clock duration.
  - `run_elapsed_time_ms` is the lifecycle run phase as reported by
    `GraphExecutor::Run()`.
  - first-run timings include setup noise unless warm-up iterations are used.
  - current GPU lane is Metal direct DFT, not GPU FFT.
- README update stays limited to the existing DSP example/index section.

**Risks**

- Schema tests can become brittle if `ExecutionResult` grows. Keep required
  fields focused on stable benchmark interpretation.
- Standard deviation for one measured iteration should be defined as `0.0`.

**Rollback plan**

Remove schema, schema tests, and documentation additions. Comparison runner can
still emit a simpler implementation-defined report.

**CI-safe or local-only**

CI-safe.

## PR4: Optional Local Strict Performance Gate

**Purpose**

Add an explicitly enabled local-only gate that can fail when the Metal lane is
not faster than the CPU lane by a configured threshold. This must never be the
default CI behavior.

**Files to touch**

- CPU-vs-Metal comparison runner from PR2.
- `examples/DSP/test/test_dsp_cpu_vs_metal_benchmark.cpp`
- `examples/DSP/CMakeLists.txt`
- `docs/dsp/spectrum_demo.md`
- `README.md` only in the existing DSP example/index section.

**Files to delete**

- None.

**Tests to add**

- `DspCpuVsMetalBenchmarkTest.StrictGateDisabledByDefault`
- `DspCpuVsMetalBenchmarkTest.StrictGateRequiresExplicitEnvironmentVariable`
- `DspCpuVsMetalBenchmarkTest.StrictGateRequiresNativeMetal`
- `DspCpuVsMetalBenchmarkTest.StrictGateUsesExecuteElapsedTiming`
- `DspCpuVsMetalBenchmarkTest.StrictGateFailureIsReportedAsLocalOnly`

**Tests to delete**

- None.

**Acceptance criteria**

- Strict mode requires an explicit opt-in such as
  `GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1`.
- Optional threshold can be set with a variable such as
  `GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO`.
- Default mode never fails only because Metal is slower.
- Strict mode uses consolidated `Execute()` timing fields from
  `ExecutionResult`, not ad hoc timers.
- Strict mode fails clearly when:
  - native Metal is unavailable;
  - GPU correctness/parity fails;
  - measured speedup ratio is below the configured threshold.
- Strict mode report includes `mode: gate_enforced`.
- CTest lane, if added, is disabled or labeled local-only and is not part of
  default CI.

**Risks**

- Performance can vary across macOS versions, thermal state, and host load.
  Keep the gate local-only and opt-in.
- Current direct DFT may not outperform CPU for `N=256`. That must be a valid
  benchmark finding, not a default CI failure.

**Rollback plan**

Remove strict-mode options, tests, and docs. Keep the informational comparison
runner.

**CI-safe or local-only**

Local-only for the strict gate. CI-safe tests may verify that the gate is off by
default.

## PR5: Truth-In-Labeling Performance Documentation Audit

**Purpose**

Add final guardrails proving active docs and tests avoid unqualified performance
claims and do not describe the Metal DFT lane as GPU FFT.

**Files to touch**

- `libgraph/test/unit/test_dsp_gpu_truth_in_labeling.cpp`
- `examples/DSP/test/test_dsp_cpu_vs_metal_benchmark.cpp`
- `docs/dsp/spectrum_demo.md`
- `README.md` only if the audit finds wording that needs correction.

**Files to delete**

- None.

**Tests to add**

- `DspGpuTruthInLabelingTest.PerformanceDocsRequireMeasuredOnHostQualifier`
- `DspGpuTruthInLabelingTest.PerformanceDocsDoNotClaimGeneralGpuSuperiority`
- `DspGpuTruthInLabelingTest.MetalDftIsNeverDocumentedAsGpuFft`
- `DspGpuTruthInLabelingTest.DefaultCiDoesNotRequireSpeedup`
- `DspGpuTruthInLabelingTest.PerformanceReportsUseExecuteResultTiming`

**Tests to delete**

- None.

**Acceptance criteria**

- Active docs use qualified wording such as “measured on this host/config”.
- Active docs do not say or imply “Metal is faster” without tying the claim to
  a concrete report.
- `MetalSpectrumDftNode` remains described as direct DFT, not GPU FFT.
- Default CI-safe tests do not require native Metal speedup.
- Performance documentation names `GraphExecutor::Execute()` consolidated
  timing as the source of comparison data.
- No algorithm behavior changes are introduced.

**Risks**

- Text-search guardrails can overmatch unrelated content. Keep patterns narrow
  to DSP performance docs, README DSP section, and benchmark tests.

**Rollback plan**

Remove the audit tests and revert any documentation wording changes. Comparison
functionality remains.

**CI-safe or local-only**

CI-safe.
