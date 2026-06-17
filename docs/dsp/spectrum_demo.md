# DSP Spectrum Demo (CPU-Only)

This document describes the current GraphX DSP spectrum demo lane.

## Purpose

The demo is a deterministic, CPU-only runtime/dataflow demonstration using the
existing GraphX graph builder, plugin loading, JSON config, and executor flow.

Demo graph shape:

`SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>`

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

## Truth In Labeling

- The current demo is CPU-only.
- The current transform path is direct DFT.
- The current transform path is not a GPU FFT.
- The current transform path is not an external FFT library.

## Current Gaps

- CPU-only execution.
- Direct DFT path, not FFT library/GPU FFT.
- Only `SpectrumSinkNode<256>` plugin lane is demonstrated.
- No real-time audio input lane.
- No Metal execution lane for DSP yet.

## Future Extension Boundaries (Not Implemented Here)

- Real spectrogram image sink.
- Multi-frame/chirp fixture.
- CPU-vs-Metal parity harness.
- Real Metal kernel or Metal Performance Shaders FFT if supported.
- Performance instrumentation comparison.

## Guardrail Intent

PR3 guardrail tests ensure:

- DSP demo config and runner remain CPU-only in user-facing behavior.
- No Metal/GPU node types are introduced into the DSP demo config.
- User-facing text keeps CPU-only and direct-DFT truth-in-labeling.
