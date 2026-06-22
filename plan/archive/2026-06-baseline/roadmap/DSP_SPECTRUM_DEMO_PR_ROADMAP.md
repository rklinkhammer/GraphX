# DSP Spectrum Demo PR Roadmap

Inputs:

- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_SIMPLIFIER_REPORT.md`
- Current repository state
- `libdsp/include/dsp/SineSignalNode.hpp`
- `libdsp/include/dsp/SineWaveGenerator.hpp`
- `libdsp/include/dsp/FFTNode.hpp`
- `libdsp/include/dsp/FFTManager.hpp`
- `libdsp/include/dsp/SpectrumSinkNode.hpp`
- `libdsp/plugins/CMakeLists.txt`
- `libgraph/test/unit/test_sine_signal_node_standalone.cpp`
- Existing `GraphBuilder`, `GraphExecutorBuilder`, plugin loading, JSON config, executor completion, and SAR example runtime patterns

Scope: planning only. Do not implement code from this document directly. Each PR must compile and test independently, avoid compatibility shims, and keep the first DSP demo CPU-only and deterministic.

Target demo shape:

`SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>`

The first maintained demo uses packet size `256` because sine, FFT, and spectrum sink plugins already exist for that size.

## PR1: CPU DSP Graph Config And Runtime Integration Test

Purpose:

- Prove the existing `libdsp` sine, FFT, and spectrum sink nodes can run as a real GraphX graph through JSON config, plugin loading, `GraphBuilder`/`GraphExecutorBuilder`, executor execution, and completion signaling.
- Establish the deterministic CPU reference chain before any GPU/Metal DSP work is attempted.

Files to touch:

- `libdsp/config/dsp_sine_fft_spectrum_256.json` or another repository-consistent DSP config location chosen during implementation.
- `libgraph/test/unit/test_dsp_spectrum_graph_runtime.cpp` or `libgraph/test/integration/test_dsp_spectrum_graph_runtime.cpp`, depending on current test ownership conventions.
- `libgraph/test/CMakeLists.txt`.
- `libdsp/plugins/CMakeLists.txt` only if plugin target metadata or test wiring needs a narrow update.

Files to delete:

- None.

Tests to add:

- Graph-builder test that loads the DSP JSON config with the existing `sine_signal_node_256`, `fft_node_256`, and `spectrum_sink_node_256` plugins.
- Executor test that runs the graph and observes successful completion using existing GraphX executor completion conventions.
- Integration assertion that the sink captures at least one `MagnitudePacket<float, 256>`.
- Deterministic peak test for a known tone, initially `1000 Hz` at `48000 Hz` sample rate, allowing bin-resolution tolerance.
- Test that the graph uses only CPU DSP nodes and no Metal/GPU node types.

Tests to delete:

- None.

Acceptance criteria:

- The DSP graph config builds with the existing GraphX JSON/runtime path.
- The graph executes successfully in CI.
- `SpectrumSinkNode<float, 256>` receives spectrum output.
- The detected peak is near the configured sine frequency within one FFT bin or a documented deterministic tolerance.
- No GPU, Metal, audio input, image sink, or new FFT implementation is added.
- The project compiles and the focused DSP graph test passes.

Risks:

- `SineWaveGenerator` is currently an infinite generator; the implementation must use existing executor completion controls or add a narrow deterministic stop mechanism without redesigning `libdsp`.
- The direct DFT plus windowing may place the strongest bin near, not exactly at, `1000 Hz`.
- Plugin type names may need careful matching because the plugin info advertises template-specific names while instances expose generic node names.

Rollback plan:

- Remove the DSP graph config, the new integration test, and the CMake test wiring.

CI-safe or local-only:

- CI-safe.

## PR2: DSP Demo Runner And Deterministic Spectrum Artifacts

Purpose:

- Add a user-runnable DSP demo that follows the existing example runtime pattern: config path, plugin directory, optional additional plugin directories, executor timeout, graph execution, and summary output.
- Emit small deterministic artifacts that make the signal-processing result inspectable without requiring PNG generation or GPU work.

Files to touch:

- `examples/DSP/CMakeLists.txt` if a new DSP example package is the clearest convention.
- `examples/DSP/src/main.cpp` or a similarly named `dsp_spectrum_demo` executable source.
- `examples/DSP/config/dsp_sine_fft_spectrum_256.json` if examples own runnable configs; otherwise reuse the PR1 config location.
- Top-level `CMakeLists.txt` only if adding `examples/DSP` requires a new guarded subdirectory option.
- DSP runtime helper code only if needed to resolve `SpectrumSinkNode<float, 256>` from the graph manager cleanly.
- Tests for the new executable or runner.

Files to delete:

- None.

Tests to add:

- Executable smoke test that runs the demo with the DSP config and plugin directory.
- Artifact test that verifies a JSON summary is written when an output path is requested.
- Summary schema test covering:
  - `frame_count`
  - `peak_frequency_hz`
  - `peak_magnitude`
  - `sample_rate_hz`
  - `fft_size`
  - `window_type`
  - node diagnostics or metrics fields available from existing nodes
- Optional spectrum-bin CSV/text artifact test if implemented as a small deterministic output.

Tests to delete:

- None.

Acceptance criteria:

- A user can run one DSP demo command after building the project.
- The runner uses the same GraphX executor/plugin/config style as existing examples.
- The JSON summary is deterministic enough for CI schema and tolerance checks.
- The demo clearly reports CPU-only execution.
- No PNG image output is required.
- No GPU, Metal, real-time audio input, or new FFT implementation is added.
- The project compiles and the runner smoke tests pass.

Risks:

- Adding a new `examples/DSP` package may require top-level CMake option decisions.
- Resolving the typed sink from plugin-wrapped graph nodes may need a small helper; keep it local to the demo or test.
- If the executor timeout is too short or the sine source does not stop deterministically, the runner can become flaky.

Rollback plan:

- Remove the DSP example executable/package, generated config copy if any, artifact-writing code, and runner tests.

CI-safe or local-only:

- CI-safe.

## PR3: DSP Demo Documentation And CPU-Only Guardrails

Purpose:

- Document the DSP spectrum demo as a CPU signal-processing/runtime demonstration, not a GPU or Metal demonstration.
- Add guardrails so later work cannot accidentally claim GPU/Metal DSP support before a real backend implementation exists.

Files to touch:

- `docs/dsp/spectrum_demo.md` or a repository-consistent docs location.
- `README.md` only if project examples are indexed there.
- DSP demo tests or documentation tests.
- Existing Metal truth-in-labeling docs only if they contain a general examples matrix that should mention DSP CPU-only status.

Files to delete:

- None.

Tests to add:

- Documentation/link test if the repository has existing doc validation.
- Guardrail test that the DSP spectrum demo config and runner do not include Metal/GPU node types.
- Guardrail test that user-facing DSP demo text states the current implementation is CPU-only and uses direct DFT, not a GPU FFT.

Tests to delete:

- None.

Acceptance criteria:

- Documentation shows how to build and run the DSP demo.
- Documentation states the current gaps:
  - direct DFT, not an FFT library or GPU FFT;
  - CPU-only;
  - only `SpectrumSinkNode<256>` plugin exists;
  - no real-time audio input;
  - no Metal execution yet.
- Documentation identifies future extension boundaries without planning implementation details.
- Guardrails prevent the CPU-only demo from being marketed as Metal/GPU.
- The project compiles and documentation/guardrail tests pass.

Risks:

- Documentation can drift if PR2 runner arguments change.

Rollback plan:

- Remove the DSP docs and guardrail tests.

CI-safe or local-only:

- CI-safe.

## Future Boundary: Real Spectrogram And Metal DSP

Purpose:

- Capture follow-up boundaries without pulling them into the CPU demo plan.

Files to touch:

- None in this roadmap.

Files to delete:

- None.

Tests to add:

- None in this roadmap.

Tests to delete:

- None.

Acceptance criteria:

- Future work remains explicitly out of scope for PR1 through PR3.
- Candidate future work is limited to:
  - real spectrogram image sink;
  - multi-frame tone/chirp fixture;
  - CPU-vs-Metal parity harness;
  - real Metal kernel or Metal Performance Shaders FFT if supported;
  - performance instrumentation comparison.

Risks:

- Pulling Metal or image-output work into the CPU demo would make the first proof too broad.

Rollback plan:

- No implementation to roll back.

CI-safe or local-only:

- Not applicable.
