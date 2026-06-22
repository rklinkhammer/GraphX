# DSP GPU Spectrum PR1 Implementer Report

## Files changed

- `libdsp/include/dsp/DspGpuBufferLayout.hpp`
- `libgraph/test/unit/test_dsp_gpu_buffer_layout.cpp`

## Files deleted

- None.

## Tests added

- `DspGpuBufferLayoutTest.IqPacketUsesContiguousComplexFloatPairLayout`
- `DspGpuBufferLayoutTest.MagnitudePacketUsesContiguousFloatBinLayout`
- `DspGpuBufferLayoutTest.IqControlTokenCarriesMessageSidecarSeparatelyFromTransport`
- `DspGpuBufferLayoutTest.MagnitudeControlTokenCarriesPacketSidecarSeparatelyFromTransport`
- `DspGpuBufferLayoutTest.TransportMetadataDoesNotDefineDspIdentity`

## Tests removed

- None.

## Build/test command

```bash
cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit

./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit \
  --gtest_filter='DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'
```

Result: passed.

## Implementation notes

- Added a narrow header-only DSP GPU layout contract for the 256-sample lane.
- IQ device layout is `Float32` rank-2 `[sample, component]` with contiguous I/Q pairs.
- Magnitude device layout is `Float32` rank-1 `[bin]` with contiguous magnitude bins.
- Tests prove byte counts, tensor layout, token sidecar preservation, and separation of DSP identity from accelerator metadata.

## Remaining follow-up work

- PR2 should add `DspIqH2DNode<256>` using this layout contract.
- PR3 should add the real Metal DFT node.
- PR4 should add `DspMagnitudeD2HNode<256>`.

## Out of scope intentionally not added

- No H2D/D2H nodes.
- No Metal kernel.
- No GPU graph config.
- No CPU DSP graph semantic change.
- No CPU `FFTNode` removal or rename.
