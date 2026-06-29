# DSP GPU Spectrum PR3 Implementer Report

## PR

PR3 from `plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md`: Real Metal Spectrum DFT Node.

## Files Changed

- `libdsp/include/dsp/MetalSpectrumDftNode.hpp`
- `libdsp/src/dsp/MetalSpectrumDftNode.cpp`
- `libdsp/plugins/metal_spectrum_dft_node_256_plugin.cpp`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/CMakeLists.txt`
- `libgraph/test/unit/test_metal_spectrum_dft_node.cpp`

## Files Deleted

- None.

## Tests Added

- `MetalSpectrumDftNodeTest.RegistersInlineMetalDftKernelDescriptor`
- `MetalSpectrumDftNodeTest.FailsWithoutValidDeviceInput`
- `MetalSpectrumDftNodeTest.FailsWithoutRequiredIqSidecar`
- `MetalSpectrumDftNodeTest.LaunchesKernelAndProducesDeviceBackedMagnitudeToken`
- `MetalSpectrumDftNodeTest.PluginRegistrationExposesMetalSpectrumDftNode256`
- `MetalSpectrumDftNodeGuardrailTest.DoesNotReferenceCpuFftManager`

## Tests Removed

- None.

## Implementation Notes

- Added `MetalSpectrumDftNode<256>` as a Metal-backed direct DFT spectrum transform.
- The node consumes `ControlToken<graph::message::Message>` with a device-backed DSP IQ view and emits `ControlToken<MagnitudePacket<float, 256>>` with a device-backed magnitude view.
- The node registers an inline Metal kernel descriptor named `graphx_dsp_metal_spectrum_dft_256`.
- The inline kernel computes a direct DFT over 256 contiguous complex float IQ samples and writes 128 contiguous float magnitude bins.
- The node launches through `IMetalKernelCapability`, populates a valid output `DeviceBufferView`, and populates a valid `KernelTicket` with `has_kernel_ticket = true`.
- Diagnostics expose backend, kernel registration, kernel id, input/output device view evidence, kernel-ticket evidence, byte counts, and `direct_dft` algorithm labeling.
- Plugin registration was added for `MetalSpectrumDftNode<256>`.
- A guardrail test proves the new node implementation/plugin do not reference `FFTManager` or `FFTManager::ProcessPacket`.

## Out Of Scope Confirmed

- Did not add D2H.
- Did not add a GPU graph config.
- Did not change CPU DSP graph config semantics.
- Did not change or rename CPU `FFTNode`.
- Did not call CPU `FFTManager`.
- Did not name the new node FFT.

## Build And Test Commands

```bash
cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit

./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*:DspIqH2DNodeTest.*:DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'

./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit
```

## Validation Result

- Focused DSP GPU/CPU guardrail suite: passed, 21 tests.
- Full `test_libgraph_unit`: passed, 985 tests passed, 2 skipped, 1 disabled.

## Remaining Follow-Up Work

- PR4 should add the matching DSP magnitude D2H node.
- A later PR should assemble the GPU DSP graph only after the D2H boundary exists.
