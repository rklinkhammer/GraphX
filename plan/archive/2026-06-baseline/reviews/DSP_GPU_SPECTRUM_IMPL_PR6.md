# DSP GPU Spectrum PR6 Implementer Report

## Scope

Implemented exactly PR6 from `plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md`: CPU-vs-GPU Spectrum Parity.

## Files Changed

- `libgraph/test/unit/test_dsp_gpu_spectrum_parity.cpp`
  - Added CPU-vs-GPU parity tests using the existing CPU DSP graph config and PR5 GPU DSP graph config.
  - Runs both lanes through `GraphExecutorBuilder` and existing plugin/runtime mechanisms.
  - Compares peak frequency, peak magnitude, and selected magnitude bins.
  - Documents deterministic tolerances in the test source.
  - Preserves the existing convention where the configured negative complex tone is expected to produce a positive 1 kHz peak.
  - Skips clearly when native Metal DSP runtime support is unavailable.
- `libdsp/src/dsp/MetalSpectrumDftNode.cpp`
  - Aligned the Metal direct DFT magnitude output with the existing CPU spectrum reference by applying the same Hann window shape and sample-count normalization.
  - Kept the node as `MetalSpectrumDftNode`; no FFT rename or CPU FFT substitution was introduced.
- `libgraph/test/unit/test_metal_spectrum_dft_node.cpp`
  - Added guardrail assertions that the Metal kernel source includes Hann-windowing and sample-count normalization evidence.

## Files Deleted

None.

## Tests Added

- `DspGpuSpectrumParityTest.PeakFrequencyMatchesCpuReference`
- `DspGpuSpectrumParityTest.PeakMagnitudeMatchesCpuWithinTolerance`
- `DspGpuSpectrumParityTest.SelectedMagnitudeBinsMatchCpuWithinTolerance`
- `DspGpuSpectrumParityTest.SkipsClearlyWhenMetalUnavailable`

No CMake source-list edit was required because `libgraph/test/CMakeLists.txt` discovers `unit/test_*.cpp` with `CONFIGURE_DEPENDS`.

## Tests Removed

None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit
```

Result: passed.

```bash
./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=DspGpuSpectrumParityTest.*:DspGpuSpectrumGraphRuntimeTest.*:DspMagnitudeD2HNodeTest.*:MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*:DspIqH2DNodeTest.*:DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'
```

Result: 33 tests ran, 28 passed, 5 skipped because native Metal was unavailable in this environment.

```bash
./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit --gtest_brief=1
```

Result: 999 tests ran, 992 passed, 7 skipped, 0 failed.

## Remaining Follow-Up Work

- Re-run the PR6 parity tests on a host with native Metal device access so the CPU-vs-GPU comparisons execute instead of skipping.
- PR7 documentation/README work remains out of scope for this PR.
