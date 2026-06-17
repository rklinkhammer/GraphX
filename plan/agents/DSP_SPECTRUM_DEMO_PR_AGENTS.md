# DSP Spectrum Demo PR Agents

Use these prompts with `plan/agents/GRAPHX_SAR_AGENT_ROLES.md` and
`plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md`.

Global constraints for every PR:

- Implement or verify exactly the named PR.
- Use the existing GraphX graph builder, executor, plugin loading, JSON config, and completion signaling patterns.
- Preserve the first demo shape: `SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>`.
- Keep the first demo CPU-only and deterministic.
- Do not add GPU, Metal, real-time audio input, PNG/image output, or a new FFT implementation unless a specific PR says so.
- Do not redesign `libdsp`.
- Do not introduce compatibility shims or duplicate demo paths.
- Prefer packet size `256` because `SineSignalNode<256>`, `FFTNode<float, 256>`, and `SpectrumSinkNode<float, 256>` plugins already exist.
- Keep SAR architecture reports as context only; do not impose SAR token architecture on DSP unless the roadmap explicitly requires it.
- Do not start future PR work.
- Stop after the requested implementer or verifier report.

---

## PR1: CPU DSP Graph Config And Runtime Integration Test

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR1 from plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md: CPU DSP Graph Config And Runtime Integration Test.

Use the implementer prompt from: plan/agents/DSP_SPECTRUM_DEMO_PR_AGENTS.md

Scope:
- Add a graph JSON config for the CPU DSP chain:
  SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>.
- Use the existing `sine_signal_node_256`, `fft_node_256`, and `spectrum_sink_node_256` plugins.
- Add a GraphX graph-builder/executor integration test that loads the JSON config through the existing runtime path.
- Verify executor completion using existing GraphX completion conventions.
- Verify `SpectrumSinkNode<float, 256>` captures at least one `MagnitudePacket<float, 256>`.
- Verify deterministic peak detection for a known sine frequency, such as 1000 Hz at 48000 Hz sample rate, using a documented bin-resolution tolerance.
- Verify the config/test path uses only CPU DSP nodes and no Metal/GPU node types.
- Update CMake test wiring as needed.

Do not add a demo executable or runner; that is PR2.
Do not add output artifacts; that is PR2.
Do not add documentation/guardrail docs; that is PR3.
Do not add GPU, Metal, real-time audio input, image output, or a new FFT implementation.
Do not redesign `libdsp`.
Do not introduce compatibility shims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_SPECTRUM_DEMO_IMPL_PR1.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR1 from plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md: CPU DSP Graph Config And Runtime Integration Test.

Use the verifier prompt from: plan/agents/DSP_SPECTRUM_DEMO_PR_AGENTS.md

Required checks:
- A CPU DSP graph JSON config exists for:
  SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>.
- The config uses the existing 256-size DSP plugin path and does not require new DSP node contracts.
- A graph-builder/executor integration test loads the config through existing GraphX runtime mechanisms.
- The test verifies executor completion using existing completion conventions.
- The test verifies `SpectrumSinkNode<float, 256>` captures at least one spectrum frame.
- The test verifies peak detection near the configured sine frequency with deterministic tolerance.
- The config and tests do not include Metal/GPU node types.
- The focused DSP graph test passes, or any failure is clearly unrelated and documented.
- No demo executable, output artifact lane, docs/guardrail docs, GPU/Metal work, real-time audio input, image output, new FFT implementation, or `libdsp` redesign was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_SPECTRUM_DEMO_VERIFY_PR1.md.
```

---

## PR2: DSP Demo Runner And Deterministic Spectrum Artifacts

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR2 from plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md: DSP Demo Runner And Deterministic Spectrum Artifacts.

Use the implementer prompt from: plan/agents/DSP_SPECTRUM_DEMO_PR_AGENTS.md

Prerequisite:
- PR1 must already provide a working CPU DSP graph config and graph-builder/executor integration test.

Scope:
- Add a user-runnable DSP spectrum demo executable or runner using existing project conventions.
- The runner must use the same GraphX runtime style as existing examples:
  config path, plugin directory, optional additional plugin directories, executor timeout, graph execution, and completion reporting.
- Use the PR1 CPU DSP chain and packet size 256.
- Add deterministic JSON summary output when an output path is requested.
- JSON summary must include, where available:
  - frame_count
  - peak_frequency_hz
  - peak_magnitude
  - sample_rate_hz
  - fft_size
  - window_type
  - node diagnostics or metrics fields available from existing nodes
- Add an executable smoke test.
- Add an artifact/schema test for the JSON summary.
- Optionally add a small deterministic text/CSV spectrum-bin artifact only if it remains narrow and CI-safe.
- Update CMake wiring as needed.

Do not change the PR1 graph semantics except as needed for runner reuse.
Do not add PNG/image output.
Do not add GPU, Metal, real-time audio input, or a new FFT implementation.
Do not redesign `libdsp`.
Do not introduce compatibility shims.
Do not add documentation/CPU-only guardrail docs; that is PR3.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_SPECTRUM_DEMO_IMPL_PR2.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR2 from plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md: DSP Demo Runner And Deterministic Spectrum Artifacts.

Use the verifier prompt from: plan/agents/DSP_SPECTRUM_DEMO_PR_AGENTS.md

Required checks:
- A user-runnable DSP spectrum demo executable or runner exists.
- The runner uses existing GraphX config/plugin/executor conventions.
- The runner uses the PR1 CPU DSP chain and packet size 256.
- The runner reports CPU-only execution.
- The runner can write a deterministic JSON summary when an output path is requested.
- The JSON summary includes frame count, peak frequency, peak magnitude, sample rate, FFT size, window type, and available diagnostics/metrics fields.
- Smoke and artifact/schema tests exist and pass, or failures are clearly unrelated and documented.
- No PNG/image output was required.
- No GPU, Metal, real-time audio input, new FFT implementation, `libdsp` redesign, compatibility shim, or PR3 documentation/guardrail work was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_SPECTRUM_DEMO_VERIFY_PR2.md.
```

---

## PR3: DSP Demo Documentation And CPU-Only Guardrails

### Implementer Agent

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Implement exactly PR3 from plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md: DSP Demo Documentation And CPU-Only Guardrails.

Use the implementer prompt from: plan/agents/DSP_SPECTRUM_DEMO_PR_AGENTS.md

Prerequisites:
- PR1 must already provide the CPU DSP graph config and runtime integration test.
- PR2 must already provide the runnable DSP demo and deterministic JSON summary output.

Scope:
- Add documentation for the DSP spectrum demo in a repository-consistent docs location.
- Document how to build and run the DSP demo.
- Document the demo shape:
  SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>.
- State clearly that the current demo is CPU-only.
- State clearly that the current FFT path is a direct DFT implementation, not a GPU FFT or external FFT library.
- State current gaps:
  - CPU-only;
  - direct DFT, not FFT library/GPU FFT;
  - only `SpectrumSinkNode<256>` plugin exists;
  - no real-time audio input;
  - no Metal execution yet.
- Document future extension boundaries without implementing them:
  - real spectrogram image sink;
  - multi-frame/chirp fixture;
  - CPU-vs-Metal parity;
  - real Metal kernel or Metal Performance Shaders FFT if supported;
  - performance instrumentation comparison.
- Add guardrail tests that the DSP demo config and runner do not include Metal/GPU node types.
- Add guardrail tests or doc checks that user-facing DSP demo text states CPU-only and direct DFT truth-in-labeling.
- Update README only if existing project examples are indexed there.

Do not change the DSP runtime behavior unless needed for narrow guardrail testability.
Do not add GPU, Metal, real-time audio input, PNG/image output, or a new FFT implementation.
Do not redesign `libdsp`.
Do not introduce compatibility shims.
Do not implement future extension boundaries.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/DSP_SPECTRUM_DEMO_IMPL_PR3.md.
```

### Verifier Agent

```text
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Verify exactly PR3 from plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md: DSP Demo Documentation And CPU-Only Guardrails.

Use the verifier prompt from: plan/agents/DSP_SPECTRUM_DEMO_PR_AGENTS.md

Required checks:
- DSP spectrum demo documentation exists.
- Documentation explains how to build and run the demo.
- Documentation states the demo shape:
  SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>.
- Documentation states the current demo is CPU-only.
- Documentation states the current FFT path is direct DFT, not GPU FFT or an external FFT library.
- Documentation states current gaps: CPU-only, only `SpectrumSinkNode<256>` plugin exists, no real-time audio input, and no Metal execution.
- Documentation identifies future extension boundaries without implementing them.
- Guardrail tests prove the DSP demo config and runner do not include Metal/GPU node types.
- Guardrail tests or doc checks prove user-facing DSP demo text says CPU-only and direct DFT.
- README was updated only if existing project examples are indexed there.
- No GPU, Metal, real-time audio input, PNG/image output, new FFT implementation, `libdsp` redesign, compatibility shim, or future extension implementation was added.

Stop after verifier report.
Save the report to plan/reviews/DSP_SPECTRUM_DEMO_VERIFY_PR3.md.
```
