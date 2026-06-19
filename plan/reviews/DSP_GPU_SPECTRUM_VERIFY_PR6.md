# DSP GPU Spectrum PR6 Verifier Report

## Verdict

PASS, with one runtime caveat: in this Codex session native Metal device enumeration reports `enumerated_devices=0; default_device=null`, so the GPU parity comparisons skipped clearly instead of executing. The test structure satisfies PR6, and the skip behavior is explicit and non-faking. Re-run the focused parity filter from a Metal-visible host session for executed GPU comparison evidence.

## Required Checks

- CPU-vs-GPU parity tests exist: PASS
  - `libgraph/test/unit/test_dsp_gpu_spectrum_parity.cpp` is tracked and defines:
    - `DspGpuSpectrumParityTest.PeakFrequencyMatchesCpuReference`
    - `DspGpuSpectrumParityTest.PeakMagnitudeMatchesCpuWithinTolerance`
    - `DspGpuSpectrumParityTest.SelectedMagnitudeBinsMatchCpuWithinTolerance`
    - `DspGpuSpectrumParityTest.SkipsClearlyWhenMetalUnavailable`

- Tests run CPU reference lane and GPU DFT lane on deterministic equivalent sine settings: PASS
  - CPU config: `libdsp/config/dsp_sine_fft_spectrum_256.json`
  - GPU config: `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`
  - Both use `frequency_hz: -1000.0`, `amplitude: 1.0`, and `sample_rate_hz: 48000.0`.

- Tests compare peak frequency, peak magnitude, and selected magnitude bins: PASS
  - Peak frequency compares CPU expected 1 kHz and GPU vs CPU.
  - Peak magnitude compares GPU vs CPU.
  - Selected bins include DC, adjacent low bins, peak-adjacent bins, representative mid bins, and the final bin.

- Tolerances are explicit and justified: PASS
  - Peak frequency tolerance is one FFT/DFT bin width: `48000 / 256 = 187.5 Hz`.
  - Magnitude tolerance is `max(1.0e-2, 5.0e-2 * abs(expected))`.
  - The test comment justifies this as small CPU/Metal floating-point tolerance without accepting scale or bin errors.

- Positive 1 kHz peak convention is preserved: PASS
  - Configured sine frequency remains `-1000.0`.
  - The parity test expects `kExpectedToneHz = 1000.0`.

- Metal-unavailable cases skip clearly and do not fake success: PASS
  - Each parity test gates on `MetalDspRuntimeSkipReason()`.
  - The skip message includes native Metal diagnostics.
  - Focused run in this environment skipped with `enumerated_devices=0; default_device=null`.

- Tests do not loosen tolerances enough to hide obviously incorrect GPU output: PASS
  - Peak bin equality is required.
  - One-bin frequency tolerance is bounded by DFT resolution.
  - Magnitude tolerance is narrow enough to catch scale, windowing, and bin-placement errors.

- No docs/README work, CPU `FFTNode` rename/removal, fake FFT naming, SAR type leak, or compatibility shim was added: PASS
  - No docs/README changes were found for this verification scope.
  - `FFTNode` files are unchanged for PR6 scope.
  - GPU transform remains named `MetalSpectrumDftNode`, not FFT.
  - No SAR/GOTCHA/CRSD references were found in the DSP parity path.
  - No compatibility shim was found.

## Validation Commands

```bash
./build-ninja/ninja-debug-metal-native-strict/libgraph/test/test_libgraph_unit \
  '--gtest_filter=DspGpuSpectrumParityTest.*:DspGpuSpectrumGraphRuntimeTest.*:MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*' \
  --gtest_brief=1
```

Result:

- 12 tests ran.
- 7 passed.
- 5 skipped because native Metal was unavailable in this Codex process.
- No failures.

## Notes

- `MetalSpectrumDftNode` kernel source applies Hann windowing and sample-count normalization, matching the CPU reference lane used by the parity tests.
- `MetalSpectrumDftNodeTest.RegistersInlineMetalDftKernelDescriptor` asserts the kernel source includes `hann_window`, `/ float(kSampleCount)`, and does not reference `FFTManager`.
- The current environment cannot prove executed GPU parity, only correct skip behavior. A host session with Metal-visible execution should re-run `DspGpuSpectrumParityTest.*` to confirm the comparisons execute and pass.
