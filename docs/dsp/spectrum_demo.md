# DSP Spectrum Demo And GPU DFT Lane

This document describes the current GraphX DSP spectrum lanes and the
truth-in-labeling boundaries for CPU, Metal DFT, and future FFT work.

## Purpose

The user-runnable demo is a deterministic, CPU-only runtime/dataflow
demonstration using the existing GraphX graph builder, plugin loading, JSON
config, and executor flow.

CPU reference graph shape:

`SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>`

The CPU lane is the correctness reference. The current `FFTNode<256>` path uses
the existing CPU direct DFT implementation and Hann-windowed deterministic
spectrum output.

GPU Metal direct DFT graph shape:

`SineSignalNode<256> -> DspIqH2DNode<256> -> MetalSpectrumDftNode<256> -> DspMagnitudeD2HNode<256> -> SpectrumSinkNode<float, 256>`

The GPU lane is a separate explicit graph. It keeps H2D, kernel execution, and
D2H visible as graph nodes. `MetalSpectrumDftNode<256>` launches a Metal direct
DFT kernel and is intentionally named DFT because it is not a true FFT.

## Build

```bash
cmake --preset ninja-debug-metal-native
cmake --build --preset build-debug-metal-native --target dsp_spectrum_demo
```

## Run

```bash
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  libdsp/config/dsp_sine_fft_spectrum_256.json \
  build-ninja/ninja-debug-metal-native/plugins
```

Optional deterministic summary artifact:

```bash
tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  libdsp/config/dsp_sine_fft_spectrum_256.json \
  build-ninja/ninja-debug-metal-native/plugins \
  --summary-json "$tmpdir/summary.json"
```

Optional CPU-vs-Metal execute-timing comparison:

```bash
tmpdir="$(mktemp -d)"
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --warmup-iterations 1 \
  --measured-iterations 3 \
  --executor-timeout-s 8 \
  --report-json "$tmpdir/dsp_cpu_vs_metal_report.json"
```

The comparison report uses schema
`examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json` and
remains informational by default. It is measured on the current host/config and
does not fail solely because Metal is slower or unavailable.

Optional local-only strict speedup gate:

```bash
GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1 \
GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO=1.10 \
./build-ninja/ninja-debug-metal-native/examples/DSP/graphx-dsp-spectrum-demo \
  --compare-cpu-metal \
  --cpu-config libdsp/config/dsp_sine_fft_spectrum_256.json \
  --gpu-config libdsp/config/dsp_sine_metal_dft_spectrum_256.json \
  --plugin-dir build-ninja/ninja-debug-metal-native/plugins \
  --warmup-iterations 1 \
  --measured-iterations 3 \
  --executor-timeout-s 8 \
  --report-json "$tmpdir/dsp_cpu_vs_metal_strict_report.json"
```

The strict gate is local-only and must be explicitly enabled with
`GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1`. It fails when native Metal is unavailable,
CPU/GPU parity fails, or the measured `run_elapsed_time_ms` speedup ratio is
below `GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO`. It is not part of default CI.

## Execute-Timing Report Interpretation

CPU-vs-Metal comparison timing comes from the `ExecutionResult` returned by
`GraphExecutor::Execute()`. The report uses the same field names as the runtime
result:

- `elapsed_time_ms`: total `Execute()` wall-clock duration for one graph run,
  including init, start, run, stop, and join.
- `run_elapsed_time_ms`: the executor `Run()` lifecycle phase duration. This is
  the closest runtime-owned measurement for graph execution wait/completion,
  but it still includes completion-policy wait and shutdown latency.
- `init_elapsed_time_ms`, `start_elapsed_time_ms`, `stop_elapsed_time_ms`, and
  `join_elapsed_time_ms`: the corresponding lifecycle phase durations copied
  from the executor result.
- In strict mode, the speedup gate uses `run_elapsed_time_ms` from
  `GraphExecutor::Execute()` results. It does not use ad hoc timers.

Warm-up iterations run before measured iterations and are excluded from summary
statistics. First-run timings can include plugin loading, graph construction,
Metal setup, and other one-time effects, so compare reports should be read as
measurements for the recorded host/config rather than general performance
claims.

## Truth In Labeling

- The runnable `graphx-dsp-spectrum-demo` command above is CPU-only by default.
- The CPU reference lane uses `FFTNode<256>` but currently computes a direct DFT
  path through the existing CPU implementation.
- The GPU Metal lane is `MetalSpectrumDftNode<256>`, a real Metal direct DFT.
- `MetalSpectrumDftNode<256>` is not a GPU FFT.
- A future true Metal FFT lane must use FFT naming only after implementing a
  real FFT algorithm, such as a staged FFT kernel or a supported Metal
  Performance Shaders FFT path.
- The current lanes do not use an external FFT library.
- The CPU-vs-Metal report is an execute-timing comparison for the CPU direct DFT
  lane and the GPU Metal direct DFT lane; it is not a GPU FFT benchmark.

## Current Gaps

- CPU and GPU lanes are direct DFT paths, not FFT library/GPU FFT paths.
- No real-time audio input lane.
- No true Metal FFT lane yet.
- No performance claim is made for Metal DFT versus CPU DFT.

## Future Extension Boundaries (Not Implemented Here)

- Real spectrogram image sink.
- Multi-frame/chirp fixture.
- True Metal FFT implementation with FFT naming only after the algorithm exists.
- Metal Performance Shaders FFT if supported and explicitly integrated.
- Performance instrumentation comparison.

## Guardrail Intent

Guardrail tests ensure:

- The CPU demo config and runner remain CPU-only in user-facing behavior.
- GPU node names do not claim FFT when they implement direct DFT.
- GPU-labeled DSP success requires kernel-ticket diagnostics.
- `MetalSpectrumDftNode<256>` does not reference `FFTManager`.
- User-facing text distinguishes CPU direct DFT, GPU Metal direct DFT, and a
  future true Metal FFT lane.
