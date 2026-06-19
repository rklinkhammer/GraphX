# DSP GPU Spectrum PR2 Implementer Report

## Files changed

- `libdsp/include/dsp/DspIqH2DNode.hpp`
- `libdsp/src/dsp/DspIqH2DNode.cpp`
- `libdsp/plugins/dsp_iq_h2d_node_256_plugin.cpp`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/unit/test_dsp_iq_h2d_node.cpp`
- `libgraph/test/CMakeLists.txt`

## Files deleted

- None.

## Tests added

- `DspIqH2DNodeTest.PreservesTokenSidecarAndCopiesIqLayout`
- `DspIqH2DNodeTest.FailsWithoutRequiredIqPacketSidecar`
- `DspIqH2DNodeTest.FailsWhenTransferCapabilityUnavailable`
- `DspIqH2DNodeTest.FailsWhenTransferRejectsCopy`
- `DspIqH2DNodeTest.PluginRegistrationExposesDspIqH2DNode256`

## Tests removed

- None.

## Build/test command

```bash
cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit

./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit \
  --gtest_filter='DspIqH2DNodeTest.*:DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'
```

Result: passed.

## Implementation notes

- Added `DspIqH2DNode<256>` as a token-preserving DSP IQ host-to-device transfer node.
- The node consumes and emits `graph::gpu::accel::ControlToken<graph::message::Message>`.
- The node packs `IqPacket<float, 256>` sidecar data into the PR1 contiguous Float32 `[sample, component]` layout.
- The node uses Metal memory-pool and transfer capabilities for device allocation and H2D copy.
- The node populates host view, device view, lease, and transfer ticket on successful transfer.
- The node fails deterministically for missing IQ sidecar payload, missing transfer capability, allocation failure, or transfer rejection.
- Added `DspIqH2DNode<256>` plugin registration.

## Remaining follow-up work

- PR3 should add the real Metal spectrum DFT transform.
- PR4 should add `DspMagnitudeD2HNode<256>`.
- PR5 should add the explicit GPU DSP graph config.

## Out of scope intentionally not added

- No GPU spectrum transform.
- No D2H node.
- No GPU graph config.
- No CPU DSP config changes.
- No CPU `FFTNode` changes, removal, or rename.
- No SAR H2D types or SAR names.
