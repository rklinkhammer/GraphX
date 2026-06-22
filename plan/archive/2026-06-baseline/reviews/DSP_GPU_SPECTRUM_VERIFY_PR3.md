# DSP GPU Spectrum PR3 Verifier Report

## PR

PR3 from `plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md`: Real Metal Spectrum DFT Node.

## Verdict

Not fully accepted.

The implementation satisfies the main Metal DFT node, plugin, launch, diagnostics, and no-CPU-FFT guardrail requirements. One required verifier check is not fully covered: PR3 does not include a focused `MetalSpectrumDftNode<256>` test proving Metal-unavailable capability cases fail or skip clearly without fake success.

## Required Checks

| Check | Result | Evidence |
| --- | --- | --- |
| `MetalSpectrumDftNode<256>` exists and is plugin-registered. | Pass | Node declared in `libdsp/include/dsp/MetalSpectrumDftNode.hpp`; plugin target `metal_spectrum_dft_node_256` registered in `libdsp/plugins/CMakeLists.txt`; test dependency added in `libgraph/test/CMakeLists.txt`; plugin availability test exists. |
| Node name uses `Dft`, not `Fft`. | Pass | Type, file, plugin, kernel, and tests use `MetalSpectrumDftNode` / `graphx_dsp_metal_spectrum_dft_256`. |
| Node registers an inline Metal kernel descriptor or equivalent real Metal source path. | Pass | `BuildKernelDescriptor()` uses `MetalKernelSourceKind::InlineSource`; `BuildKernelSource()` emits a Metal kernel with DFT loop and magnitude output. |
| Tests prove valid device-backed input is required. | Pass | `MetalSpectrumDftNodeTest.FailsWithoutValidDeviceInput` covers missing device view and wrong backend. |
| Tests prove kernel launch evidence through a valid `KernelTicket`. | Pass | `LaunchesKernelAndProducesDeviceBackedMagnitudeToken` checks `has_kernel_ticket`, `IsValidKernelTicket`, launch grid, queue id, fake kernel launch count, and telemetry count. |
| Tests prove output has a valid Metal `DeviceBufferView`. | Pass | Same launch test checks `has_device_view`, `IsValidView`, byte count, tensor rank/shape, and device id. |
| Diagnostics expose backend, device input/output views, kernel id, and launch evidence. | Pass | Implementation diagnostics include backend, kernel registration, kernel id, input/output device view flags, and kernel ticket flag; launch test checks the key diagnostics. |
| Guardrail tests prove the node does not call `FFTManager` or `FFTManager::ProcessPacket`. | Pass | `MetalSpectrumDftNodeGuardrailTest.DoesNotReferenceCpuFftManager` scans the node header/source/plugin for both forbidden strings. |
| Metal-unavailable cases fail or skip clearly and do not fake success. | Fail | Implementation returns false/nullopt when required capabilities are absent, but PR3 lacks a focused `MetalSpectrumDftNode<256>` test for missing Metal capabilities or rejected kernel registration/launch. Existing PR2 H2D unavailable-capability tests do not verify PR3. |
| No D2H node, GPU graph config, CPU DSP config change, CPU `FFTNode` change, SAR type leak, or compatibility shim was added. | Pass | Scope scan found no D2H/GPU-config/SAR/compatibility references in PR3 files; `git diff` showed no changes to CPU DSP config or CPU FFT files. |

## Build And Test Evidence

```bash
cmake --build build-ninja/ninja-release-metal-native --target test_libgraph_unit
```

Result: passed, no work to do.

```bash
./build-ninja/ninja-release-metal-native/libgraph/test/test_libgraph_unit \
  '--gtest_filter=MetalSpectrumDftNodeTest.*:MetalSpectrumDftNodeGuardrailTest.*:DspIqH2DNodeTest.*:DspGpuBufferLayoutTest.*:DspSpectrumGraphRuntimeTest.*:SDRGraphTest.*:SineSignalNodeStandaloneTest.*'
```

Result: passed, 21 tests from 7 test suites.

## Blocking Verification Failure

Add a narrow PR3 corrective test such as:

- `MetalSpectrumDftNodeTest.FailsWhenMetalKernelCapabilityUnavailable`
- `MetalSpectrumDftNodeTest.FailsWhenKernelDescriptorRegistrationIsRejected`
- or equivalent coverage proving the node cannot produce a successful output when Metal execution capabilities are unavailable.

The production code appears to fail safely through `BindGpuCapabilities()` and `Transfer()`, but the required verifier criterion asks for evidence. That evidence is currently missing for PR3 specifically.

## Out Of Scope Confirmed

- No D2H node was added.
- No GPU graph config was added.
- CPU DSP config and CPU `FFTNode` were not changed.
- No SAR-specific types or names appeared in PR3 files.
- No compatibility shim was added.
