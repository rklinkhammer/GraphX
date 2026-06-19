# DSP GPU Spectrum PR4 Implementer Report

## PR

PR4 from `plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md`: DSP Magnitude D2H Node.

## Files Changed

- `libdsp/include/dsp/DspMagnitudeD2HNode.hpp`
- `libdsp/src/dsp/DspMagnitudeD2HNode.cpp`
- `libdsp/plugins/dsp_magnitude_d2h_node_256_plugin.cpp`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/CMakeLists.txt`
- `libgraph/test/unit/test_dsp_magnitude_d2h_node.cpp`

## Files Deleted

- None.

## Tests Added

- `DspMagnitudeD2HNodeTest.PreservesTokenMetadataAndReconstructsMagnitudePacket`
- `DspMagnitudeD2HNodeTest.FailsWithoutDeviceView`
- `DspMagnitudeD2HNodeTest.FailsWhenTransferCapabilityUnavailable`
- `DspMagnitudeD2HNodeTest.FailsWhenTransferRejectsCopy`
- `DspMagnitudeD2HNodeTest.DiagnosticsExposeCopyAndPeakEvidence`
- `DspMagnitudeD2HNodeTest.PluginRegistrationExposesDspMagnitudeD2HNode256`

## Tests Removed

- None.

## Implementation Notes

- Added `DspMagnitudeD2HNode<256>`.
- The node consumes `ControlToken<MagnitudePacket<float, 256>>` with a Metal device-backed magnitude view from the PR3 `MetalSpectrumDftNode<256>` contract.
- The node emits `ControlToken<MagnitudePacket<float, 256>>`.
- The node preserves token id, device view, kernel ticket state, and sidecar identity fields such as packet number, accumulated packet count, sample rate, and window type.
- The node copies contiguous Float32 magnitude bins back to a host view through `IMetalTransferCapability::EnqueueD2H`.
- The node reconstructs a valid host-side `MagnitudePacket<float, 256>` and computes peak bin, peak magnitude, and peak frequency after copy-back.
- Plugin registration was added for `DspMagnitudeD2HNode<256>`.

## Out Of Scope Confirmed

- Did not add a GPU graph config.
- Did not add CPU-vs-GPU parity tests.
- Did not change `SpectrumSinkNode`.
- Did not call CPU `FFTManager`.
- Did not change CPU DSP config or CPU `FFTNode`.

## Build And Test Commands

```bash
cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit

./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=DspMagnitudeD2HNodeTest.*:MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*:DspIqH2DNodeTest.*:DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'

./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit
```

## Validation Result

- Focused DSP GPU/CPU guardrail suite: passed, 27 tests.
- Full `test_libgraph_unit`: passed, 991 tests passed, 2 skipped, 1 disabled.

## Remaining Follow-Up Work

- A future PR should assemble an explicit GPU DSP graph lane using the H2D, Metal DFT, D2H, and sink nodes.
- A later parity PR should compare CPU FFT output against the GPU DFT lane with appropriate tolerance.
