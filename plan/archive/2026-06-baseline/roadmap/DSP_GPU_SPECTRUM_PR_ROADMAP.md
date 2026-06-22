# DSP GPU Spectrum PR Roadmap

## Current-State Summary

The CPU DSP lane exists and must remain the correctness reference:

```text
SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>
```

The active DSP node boundaries already use `graph::gpu::accel::ControlToken`:

- `SineSignalNode<256>` emits `ControlToken<graph::message::Message>`.
- `FFTNode<float, 256>` consumes `ControlToken<graph::message::Message>` and emits `ControlToken<MagnitudePacket<float, 256>>`.
- `SpectrumSinkNode<float, 256>` consumes `ControlToken<MagnitudePacket<float, 256>>` and keeps host-side spectrum inspection APIs.

This tokenization is necessary but not sufficient for GPU execution. A truthful GPU lane must explicitly move IQ samples to a Metal-compatible device buffer, execute a real Metal kernel, move magnitude results back to host-side packet form, and then feed the existing sink.

Target GPU lane:

```text
SineSignalNode<256>
  -> DspIqH2DNode<256>
  -> MetalSpectrumDftNode<256>
  -> DspMagnitudeD2HNode<256>
  -> SpectrumSinkNode<float, 256>
```

The first GPU algorithm should be a real Metal direct DFT for `N=256`, not a fake FFT. It must be named as DFT unless a true FFT is implemented.

## Architecture Invariants

- The CPU `FFTNode` remains in place and remains the CPU reference lane.
- GPU DSP work is a separate explicit graph lane, not hidden inside CPU `FFTNode`.
- H2D and D2H transitions are visible graph nodes.
- DSP GPU nodes preserve `ControlToken` sidecars and only use accelerator metadata for transport state.
- DSP nodes must not introduce SAR, GOTCHA, or CRSD concepts.
- GPU-labeled nodes must either launch a real Metal kernel or fail/skip clearly.
- `MetalSpectrumDftNode` must not call `FFTManager::ProcessPacket`.
- CI must not require external datasets, audio devices, or non-deterministic inputs.
- If Metal is unavailable, Metal-specific tests skip clearly; they must not fake success.

## PR1: DSP GPU Token Transfer Contracts

**Purpose**

Define the DSP host/device token contract before adding executable GPU nodes. This PR makes the byte layout and sidecar preservation rules testable.

**Files to touch**

- `libdsp/include/dsp/IqPacket.hpp`
- `libdsp/include/dsp/MagnitudePacket.hpp`
- `libdsp/include/dsp/SineSignalNode.hpp`
- `libdsp/include/dsp/FFTNode.hpp`
- `libdsp/include/dsp/SpectrumSinkNode.hpp`
- `libdsp/include/dsp/DspGpuTokenContracts.hpp` or `libdsp/include/dsp/DspGpuBufferLayout.hpp`
- `libdsp/test` or existing graph/DSP test location used by CMake
- `libdsp/CMakeLists.txt`
- `libgraph/test/CMakeLists.txt` if tests live there

**Files to delete**

- None.

**Tests to add**

- Focused DSP token contract tests covering:
  - `ControlToken<graph::message::Message>` carries an `IqPacket<float, 256>` sidecar.
  - `ControlToken<MagnitudePacket<float, 256>>` carries host-side magnitude results.
  - IQ device layout is defined as contiguous complex float pairs.
  - Magnitude device layout is defined as contiguous float magnitude bins.
  - Sidecar fields are preserved separately from `host_view`, `device_view`, tickets, and leases.

**Tests to delete**

- None.

**Acceptance criteria**

- Contract helpers/types compile without adding GPU execution.
- Tests define byte counts and layouts for `IqPacket<float, 256>` and `MagnitudePacket<float, 256>`.
- Existing CPU DSP graph tests still pass unchanged.
- No Metal kernel or H2D/D2H node is added yet.

**Risks**

- Existing `DataType` enum may not have a precise complex-float value. The plan should choose the closest existing type or add a narrowly named DSP layout helper without abusing an unrelated type.
- Doxygen-only comments may drift from tested layout if tests do not assert byte sizes.

**Rollback plan**

Remove the contract helper/header and its focused tests. CPU DSP behavior remains unchanged.

**CI-safe or local-only**

CI-safe.

## PR2: DSP IQ H2D Node

**Purpose**

Add a DSP-specific H2D node that preserves `ControlToken<graph::message::Message>` while creating valid host/device accelerator metadata for IQ sample data.

**Files to touch**

- `libdsp/include/dsp/DspIqH2DNode.hpp`
- `libdsp/src/dsp/DspIqH2DNode.cpp`
- `libdsp/plugins/dsp_iq_h2d_node_256_plugin.cpp`
- `libdsp/plugins/CMakeLists.txt`
- `libdsp/CMakeLists.txt`
- `libdsp/include/dsp/DspGpuBufferLayout.hpp`
- `libgraph/test/unit` or `libdsp/test`
- `libgraph/test/CMakeLists.txt` or equivalent test wiring

**Files to delete**

- None.

**Tests to add**

- `DspIqH2DNodeTest.PreservesTokenSidecar`
- `DspIqH2DNodeTest.PopulatesValidHostAndDeviceViews`
- `DspIqH2DNodeTest.RejectsMissingIqPacket`
- `DspIqH2DNodeTest.UsesDeterministicLayoutFor256ComplexSamples`
- Plugin availability test for `DspIqH2DNode<256>`.

**Tests to delete**

- None.

**Acceptance criteria**

- Node input and output type are `ControlToken<graph::message::Message>`.
- Output token preserves `token_id` and sidecar payload.
- Output token sets `has_host_view`, `has_device_view`, `has_lease`, and `has_transfer_ticket` according to actual transfer behavior.
- If native Metal transfer capability is unavailable, the node fails clearly instead of fabricating a device view.
- No GPU spectrum transform is added.
- CPU DSP config remains unchanged.

**Risks**

- Existing generic `H2DAsyncNodeMetal` consumes raw `HostPinnedBufferView`, not tokens. The DSP node should reuse capability services and layout rules, not force generic Metal nodes to understand DSP packets.
- Native Metal shared/private storage mode may affect host pointer visibility. Tests should validate token metadata and deterministic behavior rather than relying on raw pointer identity.

**Rollback plan**

Remove the node, plugin, and tests. CPU DSP lane remains untouched.

**CI-safe or local-only**

CI-safe when Metal stubs or native Metal capabilities are available through existing build presets. Native-only checks must skip clearly when Metal is unavailable.

## PR3: Real Metal Spectrum DFT Node

**Purpose**

Add a truthful Metal-backed DSP spectrum transform for `N=256`. This is a real GPU direct DFT lane, not a CPU FFT and not an optimized FFT.

**Files to touch**

- `libdsp/include/dsp/MetalSpectrumDftNode.hpp`
- `libdsp/src/dsp/MetalSpectrumDftNode.cpp`
- `libdsp/plugins/metal_spectrum_dft_node_256_plugin.cpp`
- `libdsp/plugins/CMakeLists.txt`
- `libdsp/CMakeLists.txt`
- `libdsp/include/dsp/DspGpuBufferLayout.hpp`
- `libgpu/include/gpu/metal/capabilities/IMetalCapabilities.hpp` only if a missing capability contract is proven necessary
- `libgpu/src/gpu/metal/native/NativeMetalCapabilities.cpp` only if inline-source support needs a narrow correction
- Focused DSP/Metal tests

**Files to delete**

- None.

**Tests to add**

- `MetalSpectrumDftNodeTest.RequiresValidDeviceInput`
- `MetalSpectrumDftNodeTest.RegistersInlineMetalKernelDescriptor`
- `MetalSpectrumDftNodeTest.LaunchesRealKernelAndRecordsTicket`
- `MetalSpectrumDftNodeTest.DoesNotCallCpuFftManager`
- `MetalSpectrumDftNodeTest.ProducesDeviceBackedMagnitudeToken`
- Plugin availability test for `MetalSpectrumDftNode<256>`.

**Tests to delete**

- None.

**Acceptance criteria**

- Node consumes a device-backed DSP IQ token and emits a device-backed `ControlToken<MagnitudePacket<float, 256>>` or a documented device-magnitude sidecar type selected by PR1.
- Node registers an inline Metal DFT kernel source through existing Metal descriptor capability.
- Node records a valid `KernelTicket` and sets `has_kernel_ticket`.
- Node output has a valid Metal `DeviceBufferView`.
- Node does not call `FFTManager`, `FFTManager::ProcessPacket`, or CPU DFT helpers.
- If native Metal cannot compile or launch the kernel, execution fails clearly.
- The node name uses `Dft`, not `Fft`.

**Risks**

- Direct DFT is computationally expensive but acceptable for `N=256` and deterministic tests.
- Existing generic `DeviceTransformNodeMetal` assumes one device buffer and byte-oriented kernels. The DSP DFT node likely needs a DSP-specific implementation with explicit input/output buffers and kernel arguments.
- Kernel source string tests can become brittle. Prefer behavioral checks plus a narrow guardrail that the node uses `MetalKernelSourceKind::InlineSource`.

**Rollback plan**

Remove the node, plugin, and tests. PR1 and PR2 remain useful for future GPU DSP work.

**CI-safe or local-only**

CI-safe with clear Metal-unavailable skips. Native Metal launch tests are conditional on existing Metal runtime availability.

## PR4: DSP Magnitude D2H Node

**Purpose**

Add a DSP-specific D2H node that copies Metal magnitude output back to host-side `MagnitudePacket<float, 256>` form and feeds `SpectrumSinkNode`.

**Files to touch**

- `libdsp/include/dsp/DspMagnitudeD2HNode.hpp`
- `libdsp/src/dsp/DspMagnitudeD2HNode.cpp`
- `libdsp/plugins/dsp_magnitude_d2h_node_256_plugin.cpp`
- `libdsp/plugins/CMakeLists.txt`
- `libdsp/CMakeLists.txt`
- `libdsp/include/dsp/DspGpuBufferLayout.hpp`
- Focused tests

**Files to delete**

- None.

**Tests to add**

- `DspMagnitudeD2HNodeTest.PreservesTokenMetadata`
- `DspMagnitudeD2HNodeTest.ReconstructsMagnitudePacketShape`
- `DspMagnitudeD2HNodeTest.ComputesPeakFrequencyAndMagnitude`
- `DspMagnitudeD2HNodeTest.RejectsMissingDeviceMagnitudeView`
- Plugin availability test for `DspMagnitudeD2HNode<256>`.

**Tests to delete**

- None.

**Acceptance criteria**

- Node consumes device-backed magnitude tokens from `MetalSpectrumDftNode<256>`.
- Node emits `ControlToken<MagnitudePacket<float, 256>>`.
- Reconstructed packet has valid sample rate, FFT/DFT size, magnitude-bin count, peak frequency, and peak magnitude.
- `SpectrumSinkNode<float, 256>` can consume the output without changes.
- No CPU FFT computation is introduced.

**Risks**

- The host packet reconstruction needs sample-rate metadata. If it is not present in the token sidecar after PR3, PR3/PR4 must define a minimal DSP spectrum metadata sidecar field without bloating `MagnitudePacket`.
- If Metal private storage is active, the D2H copy must rely on transfer capability rather than reading device memory directly.

**Rollback plan**

Remove the D2H node, plugin, and tests. Earlier GPU transfer/DFT pieces remain isolated.

**CI-safe or local-only**

CI-safe with clear Metal-unavailable skips for native transfer checks.

## PR5: GPU DSP Graph Config And Executor Integration

**Purpose**

Add the first full GPU DSP graph config and prove GraphX can execute it through the existing graph builder, plugin loader, executor, and completion conventions.

**Files to touch**

- `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/unit/test_dsp_gpu_spectrum_graph_runtime.cpp`
- `libgraph/test/CMakeLists.txt`
- `examples/DSP/CMakeLists.txt` only if the demo runner gets optional GPU config support in this PR
- `examples/DSP/src/main.cpp` only if optional runner support is included

**Files to delete**

- None.

**Tests to add**

- `DspGpuSpectrumGraphRuntimeTest.ConfigUsesExplicitGpuDspNodes`
- `DspGpuSpectrumGraphRuntimeTest.JsonTopologyRunsThroughExecutor`
- `DspGpuSpectrumGraphRuntimeTest.SpectrumSinkReceivesAtLeastOneFrame`
- `DspGpuSpectrumGraphRuntimeTest.DiagnosticsContainDeviceAndKernelEvidence`

**Tests to delete**

- None.

**Acceptance criteria**

- New JSON graph is:

```text
SineSignalNode<256>
  -> DspIqH2DNode<256>
  -> MetalSpectrumDftNode<256>
  -> DspMagnitudeD2HNode<256>
  -> SpectrumSinkNode<256>
```

- Graph has 5 nodes and 4 edges.
- Executor completion is signaled by the sink.
- Final sink receives at least one valid `MagnitudePacket<float, 256>`.
- Diagnostics expose valid device input/output views and a valid kernel ticket when Metal is available.
- Existing CPU config and CPU tests remain unchanged.

**Risks**

- Plugin type names must match resolver-visible names exactly.
- Metal runtime initialization may differ between direct unit tests and executor tests.
- Executor timeout may need a slightly larger bound than the CPU path because direct DFT is O(N²).

**Rollback plan**

Remove the GPU config and runtime test. Node-level PRs remain intact.

**CI-safe or local-only**

CI-safe with Metal-unavailable skip behavior. No external inputs.

## PR6: CPU-vs-GPU Spectrum Parity

**Purpose**

Compare CPU reference output and GPU DFT output on the same deterministic sine input.

**Files to touch**

- `libgraph/test/unit/test_dsp_gpu_spectrum_parity.cpp`
- `libgraph/test/CMakeLists.txt`
- `libdsp/config/dsp_sine_fft_spectrum_256.json`
- `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`
- Optional shared test helper under `libgraph/test/include` or `libdsp/test/include`

**Files to delete**

- None.

**Tests to add**

- `DspGpuSpectrumParityTest.PeakFrequencyMatchesCpuReference`
- `DspGpuSpectrumParityTest.PeakMagnitudeMatchesCpuWithinTolerance`
- `DspGpuSpectrumParityTest.SelectedMagnitudeBinsMatchCpuWithinTolerance`
- `DspGpuSpectrumParityTest.SkipsClearlyWhenMetalUnavailable`

**Tests to delete**

- None.

**Acceptance criteria**

- CPU and GPU lanes run from deterministic equivalent sine settings.
- Peak frequency matches within one bin width unless a narrower tolerance is justified.
- Peak magnitude and selected bins match within a documented floating-point tolerance.
- Test output identifies CPU reference config and GPU config paths.
- Metal-unavailable state is a clear skip, not a pass with fabricated data.

**Risks**

- CPU `FFTNode` currently uses a direct DFT path with Hann windowing. The GPU DFT must match windowing, scaling, and positive-frequency bin convention or the parity tolerance will become too loose.
- The configured sine uses negative frequency to produce the positive 1 kHz peak in current tests. The parity test must preserve that convention.

**Rollback plan**

Remove parity test only. GPU graph remains available but less validated.

**CI-safe or local-only**

CI-safe with Metal-unavailable skip behavior.

## PR7: DSP GPU Truth-In-Labeling And Documentation

**Purpose**

Document the CPU and GPU DSP lanes and add guardrails preventing future fake GPU claims.

**Files to touch**

- `docs/dsp/spectrum_demo.md`
- `README.md`
- `examples/DSP/src/main.cpp` if runner output needs explicit CPU/GPU mode reporting
- `examples/DSP/test/test_dsp_spectrum_demo.cpp`
- `libgraph/test/unit/test_dsp_gpu_truth_in_labeling.cpp`
- `libgraph/test/CMakeLists.txt`

**Files to delete**

- None.

**Tests to add**

- `DspGpuTruthInLabelingTest.GpuDocsStateDirectDftNotFft`
- `DspGpuTruthInLabelingTest.GpuNodeNamesDoNotClaimFft`
- `DspGpuTruthInLabelingTest.GpuLabeledNodesRequireKernelTicketDiagnostics`
- `DspGpuTruthInLabelingTest.MetalSpectrumDftDoesNotReferenceFFTManager`
- `DspGpuTruthInLabelingTest.CpuConfigRemainsCpuOnly`

**Tests to delete**

- None.

**Acceptance criteria**

- Documentation clearly distinguishes:
  - CPU direct DFT reference lane.
  - GPU Metal direct DFT lane.
  - future true Metal FFT lane.
- README and docs do not call `MetalSpectrumDftNode` a GPU FFT.
- Guardrail tests reject GPU-labeled DSP nodes that call `FFTManager`.
- Guardrail tests require kernel-ticket diagnostics for GPU DSP execution success.
- Existing CPU-only docs remain truthful and are not overwritten by GPU claims.

**Risks**

- Source-text guardrails can be brittle. Keep them narrow and targeted to active DSP GPU node files.
- Documentation must avoid implying performance superiority before instrumentation exists.

**Rollback plan**

Remove documentation updates and guardrail tests. Implementation PRs remain functional.

**CI-safe or local-only**

CI-safe.

## Future Out Of Scope

- True Metal FFT implementation.
- Metal Performance Shaders or platform FFT investigation.
- Multi-frame spectrogram image sink.
- CPU-vs-Metal performance instrumentation.
- Streaming audio input.
- Backend-neutral CUDA/SYCL DSP spectrum lanes.
- Promoting DSP-specific packet serialization into generic libgpu policy APIs before multiple domains prove the abstraction is needed.
