# DSP GPU Spectrum PR7 Implementer Report

## Scope

Implemented exactly PR7 from `plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md`: DSP GPU Truth-In-Labeling And Documentation.

## Files Changed

- `docs/dsp/spectrum_demo.md`
  - Updated the DSP documentation from CPU-only coverage to a truthful lane description covering:
    - CPU direct DFT reference lane.
    - GPU Metal direct DFT lane.
    - future true Metal FFT boundary.
  - Explicitly states `MetalSpectrumDftNode<256>` is not a GPU FFT.
  - States future FFT naming must wait for a real FFT implementation.
  - Keeps the runnable `graphx-dsp-spectrum-demo` command documented as CPU-only.
- `README.md`
  - Updated only the existing DSP example/index section.
  - Added the GPU Metal direct DFT graph shape and future true Metal FFT boundary.
  - Did not add performance claims.
- `examples/DSP/test/test_dsp_spectrum_demo.cpp`
  - Updated existing DSP documentation/runner truth-in-labeling assertions to match the new CPU reference plus GPU DFT wording.
- `libgraph/test/unit/test_dsp_gpu_truth_in_labeling.cpp`
  - Added focused guardrail tests for DSP GPU truth-in-labeling.

## Files Deleted

None.

## Tests Added

- `DspGpuTruthInLabelingTest.GpuDocsStateDirectDftNotFft`
- `DspGpuTruthInLabelingTest.GpuNodeNamesDoNotClaimFftForDftImplementation`
- `DspGpuTruthInLabelingTest.GpuLabeledNodesRequireKernelTicketDiagnostics`
- `DspGpuTruthInLabelingTest.MetalSpectrumDftDoesNotReferenceFFTManager`
- `DspGpuTruthInLabelingTest.CpuConfigRemainsCpuOnly`

No CMake source-list edit was required because `libgraph/test/CMakeLists.txt` discovers `unit/test_*.cpp` with `CONFIGURE_DEPENDS`.

## Tests Removed

None.

## Build/Test Commands

```bash
cmake --build build-ninja/ninja-debug-metal-native-strict --target test_libgraph_unit test_dsp_example_unit
```

Result: passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit \
  '--gtest_filter=DspGpuTruthInLabelingTest.*:DspGpuSpectrumGraphRuntimeTest.ConfigUsesExplicitGpuDspNodes:MetalSpectrumDftNodeGuardrailTest.*:DspSpectrumGraphRuntimeTest.ConfigUsesOnlyCpuDspNodes' \
  --gtest_brief=1
```

Result: 7 tests ran, 7 passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/examples/DSP/test/test_dsp_example_unit \
  '--gtest_filter=DspSpectrumDemoGuardrailTest.*:DspSpectrumDemoRunnerTest.*' \
  --gtest_brief=1
```

Result: 2 tests ran, 2 passed.

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit \
  '--gtest_filter=DspGpuTruthInLabelingTest.*:DspGpuSpectrumGraphRuntimeTest.*:DspGpuSpectrumParityTest.*:MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*:DspSpectrumGraphRuntimeTest.*' \
  --gtest_brief=1
```

Result: 19 tests ran, 14 passed, 5 skipped because native Metal was unavailable in this Codex process.

## Remaining Follow-Up Work

- PR7 does not implement a true Metal FFT, performance instrumentation, or spectrogram image output.
- Re-run the GPU execution/parity tests from a Metal-visible host session when executed-GPU evidence is needed; this Codex process reports no enumerated Metal device.
