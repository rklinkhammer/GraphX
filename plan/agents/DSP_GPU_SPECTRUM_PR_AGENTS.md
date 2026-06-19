# DSP GPU Spectrum PR Agents

Use these prompts with `plan/agents/GRAPHX_SAR_AGENT_ROLES.md` and
`plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md`.

Global constraints for every PR:

- Implement or verify exactly the named PR.
- Preserve the existing CPU DSP lane as the correctness reference:
  `SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>`.
- Do not remove, rename, or replace the CPU `FFTNode`.
- Add GPU work as a separate explicit graph lane.
- Keep H2D and D2H transitions visible in the graph.
- Preserve `graph::gpu::accel::ControlToken` sidecars across DSP GPU nodes.
- Do not introduce SAR, GOTCHA, CRSD, or other SAR-specific names into DSP.
- Do not fake GPU execution.
- GPU-labeled DSP nodes must launch a real Metal kernel or fail/skip clearly.
- If the first GPU algorithm is direct DFT, use `Dft` in names, not `Fft`.
- Do not call `FFTManager` from GPU-labeled DSP nodes.
- Do not require external datasets, audio devices, MATLAB, SarPy, or real GOTCHA data.
- Do not introduce compatibility shims.
- Do not start future PR work.
- Stop after the requested implementer or verifier report.

---

## PR1: DSP GPU Token Transfer Contracts

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP GPU Token Transfer Contracts.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Scope:
- Add or update DSP GPU token/buffer-layout contracts for:
  - `ControlToken<graph::message::Message>` carrying `IqPacket<float, 256>`.
  - `ControlToken<MagnitudePacket<float, 256>>`.
  - IQ layout as contiguous complex float pairs.
  - magnitude layout as contiguous float bins.
- Add focused tests proving byte counts, tensor layout, sidecar preservation, and separation of DSP identity from accelerator transport metadata.
- Use a narrow helper/header such as `libdsp/include/dsp/DspGpuBufferLayout.hpp` if needed.
- Update CMake test wiring as needed.

Do not add H2D/D2H nodes.
Do not add a Metal kernel.
Do not add a GPU graph config.
Do not change CPU DSP graph semantics.
Do not remove or rename CPU `FFTNode`.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR1.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP GPU Token Transfer Contracts.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- DSP token/buffer-layout contracts exist.
- Tests prove `IqPacket<float, 256>` maps to contiguous complex float pairs.
- Tests prove `MagnitudePacket<float, 256>` maps to contiguous float bins.
- Tests prove token sidecars are preserved separately from host/device views, tickets, and leases.
- Existing CPU DSP graph tests still pass, or failures are clearly unrelated and documented.
- No H2D/D2H node, Metal kernel, GPU graph config, CPU graph semantic change, or CPU `FFTNode` rename/removal was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR1.md.
```

---

## PR2: DSP IQ H2D Node

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP IQ H2D Node.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Prerequisite:
- PR1 token/buffer-layout contracts must already exist.

Scope:
- Add `DspIqH2DNode<256>`.
- The node must consume and emit `ControlToken<graph::message::Message>`.
- Preserve `token_id` and sidecar payload.
- Populate valid host/device view metadata, lease, and transfer ticket when transfer succeeds.
- Fail clearly if required IQ sidecar payload or Metal transfer capability is unavailable.
- Add plugin registration for `DspIqH2DNode<256>`.
- Add focused node tests and plugin availability tests.
- Update CMake wiring as needed.

Do not add a GPU spectrum transform.
Do not add D2H.
Do not add GPU graph config.
Do not change CPU DSP config or CPU `FFTNode`.
Do not use SAR H2D types or SAR names.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR2.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP IQ H2D Node.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- `DspIqH2DNode<256>` exists and is plugin-registered.
- Node input and output are `ControlToken<graph::message::Message>`.
- Tests prove token sidecar and `token_id` are preserved.
- Tests prove host/device views, lease, and transfer ticket are populated on success.
- Tests prove missing IQ payload and unavailable transfer capability fail deterministically.
- Tests prove deterministic `IqPacket<float, 256>` layout.
- Existing CPU DSP config remains unchanged.
- No GPU spectrum transform, D2H node, GPU graph config, CPU `FFTNode` change, SAR type leak, or compatibility shim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR2.md.
```

---

## PR3: Real Metal Spectrum DFT Node

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: Real Metal Spectrum DFT Node.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Prerequisites:
- PR1 token/buffer-layout contracts must already exist.
- PR2 `DspIqH2DNode<256>` must already exist.

Scope:
- Add `MetalSpectrumDftNode<256>` as a real Metal-backed direct DFT spectrum transform.
- Consume a device-backed DSP IQ token.
- Produce a device-backed magnitude token compatible with the PR4 D2H plan.
- Register and launch an inline Metal DFT kernel through existing Metal capability contracts.
- Populate valid output `DeviceBufferView`.
- Populate valid `KernelTicket` and set `has_kernel_ticket`.
- Add diagnostics proving backend, device input/output views, kernel id, and launch evidence.
- Add plugin registration for `MetalSpectrumDftNode<256>`.
- Add focused tests for valid input, invalid input, kernel descriptor registration, kernel launch evidence, output device view, and plugin availability.
- Add a guardrail test proving this node does not call `FFTManager` or `FFTManager::ProcessPacket`.

Do not call CPU `FFTManager`.
Do not name this node FFT unless a true FFT is implemented.
Do not add D2H.
Do not add GPU graph config.
Do not change CPU DSP config or CPU `FFTNode`.
Do not fake GPU success with metadata only.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR3.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: Real Metal Spectrum DFT Node.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- `MetalSpectrumDftNode<256>` exists and is plugin-registered.
- Node name uses `Dft`, not `Fft`.
- Node registers an inline Metal kernel descriptor or equivalent real Metal source path.
- Tests prove valid device-backed input is required.
- Tests prove kernel launch evidence through a valid `KernelTicket`.
- Tests prove output has a valid Metal `DeviceBufferView`.
- Diagnostics expose backend, device input/output views, kernel id, and launch evidence.
- Guardrail tests prove the node does not call `FFTManager` or `FFTManager::ProcessPacket`.
- Metal-unavailable cases fail or skip clearly and do not fake success.
- No D2H node, GPU graph config, CPU DSP config change, CPU `FFTNode` change, SAR type leak, or compatibility shim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR3.md.
```

---

## PR4: DSP Magnitude D2H Node

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR4 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP Magnitude D2H Node.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Prerequisites:
- PR1 token/buffer-layout contracts must already exist.
- PR3 `MetalSpectrumDftNode<256>` must already define its device magnitude output contract.

Scope:
- Add `DspMagnitudeD2HNode<256>`.
- Consume the device-backed magnitude token emitted by `MetalSpectrumDftNode<256>`.
- Emit `ControlToken<MagnitudePacket<float, 256>>`.
- Preserve token metadata and sidecar identity fields.
- Reconstruct a valid host-side `MagnitudePacket<float, 256>`.
- Compute or verify peak frequency and peak magnitude after copy-back.
- Add plugin registration for `DspMagnitudeD2HNode<256>`.
- Add focused tests for token preservation, packet shape, peak computation, invalid missing device view, and plugin availability.
- Update CMake wiring as needed.

Do not add GPU graph config.
Do not add CPU-vs-GPU parity tests.
Do not change `SpectrumSinkNode` unless a narrow type-contract correction is required.
Do not call CPU `FFTManager`.
Do not change CPU DSP config or CPU `FFTNode`.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR4.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR4 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP Magnitude D2H Node.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- `DspMagnitudeD2HNode<256>` exists and is plugin-registered.
- Node consumes device-backed magnitude tokens from the PR3 contract.
- Node emits `ControlToken<MagnitudePacket<float, 256>>`.
- Tests prove token metadata is preserved.
- Tests prove reconstructed packet shape, sample rate, bin count, peak frequency, and peak magnitude.
- Tests prove missing device view fails deterministically.
- `SpectrumSinkNode<float, 256>` can consume the output without broad redesign.
- No GPU graph config, parity lane, CPU `FFTNode` change, CPU `FFTManager` call in GPU path, SAR type leak, or compatibility shim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR4.md.
```

---

## PR5: GPU DSP Graph Config And Executor Integration

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR5 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: GPU DSP Graph Config And Executor Integration.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Prerequisites:
- PR2 `DspIqH2DNode<256>` must exist.
- PR3 `MetalSpectrumDftNode<256>` must exist.
- PR4 `DspMagnitudeD2HNode<256>` must exist.

Scope:
- Add a GPU DSP graph JSON config:
  SineSignalNode<256>
    -> DspIqH2DNode<256>
    -> MetalSpectrumDftNode<256>
    -> DspMagnitudeD2HNode<256>
    -> SpectrumSinkNode<256>
- Add a graph-builder/executor integration test loading the config through existing runtime mechanisms.
- Verify graph has 5 nodes and 4 edges.
- Verify executor completion using existing GraphX completion conventions.
- Verify `SpectrumSinkNode<float, 256>` receives at least one valid spectrum frame.
- Verify diagnostics include device input/output views and kernel-ticket evidence when Metal is available.
- Skip clearly when Metal is unavailable.
- Update CMake test wiring as needed.

Do not add CPU-vs-GPU parity tests; that is PR6.
Do not update docs/README; that is PR7.
Do not change CPU DSP config or CPU `FFTNode`.
Do not fake Metal availability.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR5.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR5 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: GPU DSP Graph Config And Executor Integration.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- GPU DSP JSON config exists with exactly:
  SineSignalNode<256>
    -> DspIqH2DNode<256>
    -> MetalSpectrumDftNode<256>
    -> DspMagnitudeD2HNode<256>
    -> SpectrumSinkNode<256>
- Graph-builder/executor integration test loads the config through existing runtime mechanisms.
- Test verifies 5 nodes and 4 edges.
- Test verifies executor completion.
- Test verifies `SpectrumSinkNode<float, 256>` receives at least one valid spectrum frame.
- Test verifies real GPU diagnostics when Metal is available.
- Metal-unavailable cases skip clearly and do not fake success.
- Existing CPU DSP config remains unchanged.
- No CPU-vs-GPU parity tests, docs/README work, CPU `FFTNode` change, SAR type leak, or compatibility shim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR5.md.
```

---

## PR6: CPU-vs-GPU Spectrum Parity

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR6 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: CPU-vs-GPU Spectrum Parity.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Prerequisites:
- PR5 GPU DSP graph config and executor integration must already pass or skip clearly when Metal is unavailable.

Scope:
- Add CPU-vs-GPU parity tests using the existing CPU config and the PR5 GPU config.
- Run both lanes on deterministic equivalent sine settings.
- Compare:
  - peak frequency;
  - peak magnitude;
  - selected magnitude bins.
- Define and document deterministic tolerances.
- Preserve the current convention where configured negative complex frequency produces a positive 1 kHz peak.
- Skip clearly when Metal is unavailable.
- Update CMake wiring as needed.

Do not change CPU or GPU graph semantics except for narrow deterministic configuration alignment.
Do not add docs/README work; that is PR7.
Do not rename `MetalSpectrumDftNode` to FFT.
Do not loosen tolerances to hide broken GPU output.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR6.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR6 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: CPU-vs-GPU Spectrum Parity.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- CPU-vs-GPU parity tests exist.
- Tests run CPU reference lane and GPU DFT lane on deterministic equivalent sine settings.
- Tests compare peak frequency, peak magnitude, and selected magnitude bins.
- Tolerances are explicit and justified.
- Positive 1 kHz peak convention is preserved.
- Metal-unavailable cases skip clearly and do not fake success.
- Tests do not loosen tolerances enough to hide obviously incorrect GPU output.
- No docs/README work, CPU `FFTNode` rename/removal, fake FFT naming, SAR type leak, or compatibility shim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR6.md.
```

---

## PR7: DSP GPU Truth-In-Labeling And Documentation

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR7 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP GPU Truth-In-Labeling And Documentation.

Use the implementer prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Prerequisites:
- PR5 GPU DSP graph config must exist.
- PR6 CPU-vs-GPU parity tests should exist or be documented as pending if not yet merged.

Scope:
- Update DSP documentation to distinguish:
  - CPU direct DFT reference lane;
  - GPU Metal direct DFT lane;
  - future true Metal FFT lane.
- Update README only where existing examples are indexed.
- Ensure docs do not call `MetalSpectrumDftNode` a GPU FFT.
- Add guardrail tests that:
  - GPU node names do not claim FFT when implementing DFT.
  - GPU-labeled DSP nodes require kernel-ticket diagnostics for success.
  - `MetalSpectrumDftNode` does not reference `FFTManager`.
  - CPU config remains CPU-only.
- Update example runner text only if needed to report CPU/GPU mode truthfully.

Do not implement true Metal FFT.
Do not add performance instrumentation.
Do not add spectrogram image output.
Do not change algorithm behavior.
Do not remove the CPU reference lane.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_IMPL_PR7.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR7 from plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md: DSP GPU Truth-In-Labeling And Documentation.

Use the verifier prompt from: plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md

Required checks:
- DSP docs distinguish CPU direct DFT reference lane, GPU Metal direct DFT lane, and future true Metal FFT lane.
- README was updated only where existing examples are indexed.
- Docs do not call `MetalSpectrumDftNode` a GPU FFT.
- Guardrail tests prove GPU node names do not claim FFT for DFT implementation.
- Guardrail tests prove GPU-labeled DSP success requires kernel-ticket diagnostics.
- Guardrail tests prove `MetalSpectrumDftNode` does not reference `FFTManager`.
- Guardrail tests prove CPU config remains CPU-only.
- No true Metal FFT, performance instrumentation, spectrogram image output, algorithm behavior change, CPU reference removal, SAR type leak, or compatibility shim was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_GPU_SPECTRUM_VERIFY_PR7.md.
```
