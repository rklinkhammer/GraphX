# DSP GPU Spectrum PR5 Implementer Report

## Scope

Implemented PR5: GPU DSP Graph Config And Executor Integration.

## Files Changed

- `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`
  - Added the GPU DSP graph config:
    `SineSignalNode<256> -> DspIqH2DNode<256> -> MetalSpectrumDftNode<256> -> DspMagnitudeD2HNode<256> -> SpectrumSinkNode<256>`.
  - The config declares `execution_backend: metal`, strict fallback policy, 5 nodes, and 4 edges.
- `libgraph/test/unit/test_dsp_gpu_spectrum_graph_runtime.cpp`
  - Added a GraphExecutorBuilder/runtime integration test that loads the JSON config through the existing plugin/runtime path.
  - Verifies graph node/edge count, plugin availability, executor completion, sink frame delivery, and GPU diagnostics when native Metal is available.
  - Skips clearly when native Metal runtime is unavailable.
- `libgraph/include/policies/GpuPolicy.hpp`
  - Included `GRAPHX_ENABLE_METAL_GRAPH_NODES` in the GPU policy compile guard so Metal-only graph builds still bootstrap GPU capabilities.
- `libdsp/src/dsp/DspIqH2DNode.cpp`
  - Selects the configured Metal device before queue creation during capability binding.
- `libdsp/src/dsp/MetalSpectrumDftNode.cpp`
  - Selects the configured Metal device before queue creation during capability binding.
  - Uses a real Metal context event for the kernel ticket and waits for the upstream H2D event before kernel launch.
- `libdsp/src/dsp/DspMagnitudeD2HNode.cpp`
  - Selects the Metal device before queue creation during capability binding.
  - Waits for the upstream kernel event before D2H copy-back.

## Files Deleted

None.

## Tests Added

- `DspGpuSpectrumGraphRuntimeTest.ConfigUsesExplicitGpuDspNodes`
- `DspGpuSpectrumGraphRuntimeTest.JsonTopologyRunsThroughExecutorAndSinkReceivesSpectrum`

## Tests Removed

None.

## Build/Test Commands

- `cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit`
- `./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit '--gtest_filter=DspGpuSpectrumGraphRuntimeTest.*:DspMagnitudeD2HNodeTest.*:MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*:DspIqH2DNodeTest.*:DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'`
  - Result: 28 passed, 1 skipped.
  - Skip reason: native Metal runtime unavailable in this environment: `enumerated_devices=0; default_device=null`.
- `./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit`
  - Result: 992 passed, 3 skipped, 1 disabled.

## Remaining Follow-Up Work

- Run the PR5 graph integration test on a host where native Metal runtime is visible; it is intentionally skipped in the current environment rather than exercising metadata-only fallback behavior.
- PR6 still owns CPU-vs-GPU parity testing.
- PR7 still owns documentation/README updates.
