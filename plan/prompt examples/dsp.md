```text
Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Create a PR-sized plan for a GraphX DSP spectrogram/spectrum demo using the existing libdsp nodes and the same GraphX graph builder, executor, plugin loading, JSON config, and runtime patterns used by existing examples.

Goal:
Add a clean DSP demo shape:

SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>

This should demonstrate GraphX runtime dataflow with a deterministic signal-processing chain before any GPU/Metal DSP work is attempted.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- Current repository state
- libdsp/include/dsp/SineSignalNode.hpp
- libdsp/include/dsp/SineWaveGenerator.hpp
- libdsp/include/dsp/FFTNode.hpp
- libdsp/include/dsp/FFTManager.hpp
- libdsp/include/dsp/SpectrumSinkNode.hpp
- libdsp/plugins/CMakeLists.txt
- libgraph/test/unit/test_sine_signal_node_standalone.cpp
- Existing example runtime patterns using GraphBuilder, Executor, plugin loading, JSON config, and completion signaling
- Existing examples/SAR runtime/config/test patterns only as structural references, not as SAR architecture requirements


Planning requirements:
- Do not implement code.
- Do not add GPU or Metal DSP work in this plan.
- Do not redesign libdsp.
- Do not introduce compatibility shims.
- Prefer the existing `256` packet-size path because `SpectrumSinkNode<256>` already has a plugin.
- Use existing GraphX graph builder/executor/plugin mechanisms.
- Keep the first demo CPU-only and deterministic.
- Preserve existing libdsp node contracts:
  - `SineSignalNode<256>` emits `IqPacket<float, 256>`.
  - `FFTNode<float, 256>` consumes IQ messages and emits `MagnitudePacket<float, 256>`.
  - `SpectrumSinkNode<float, 256>` consumes `MagnitudePacket<float, 256>`.
- Include an explicit future extension boundary for a later Metal/FFT/spectrogram PR, but do not plan implementation details for that PR beyond identifying the boundary.
- Each planned PR must compile and test independently.
- Each PR must include tests.
- Avoid compatibility aliases and duplicate demo paths.

Required planning coverage:
1. Add a graph JSON config for the DSP chain.
2. Add or update plugin loading so the three DSP plugins can be loaded by the demo/runtime.
3. Add a small example executable or extend an existing example runner only if that matches current project conventions.
4. Add a graph-builder/executor integration test that runs the full chain.
5. Verify deterministic peak detection for a known sine frequency, such as 1000 Hz at 48000 Hz sample rate.
6. Verify completion/runtime behavior using the same GraphX executor completion conventions used elsewhere.
7. Add spectrum/spectrogram-style output artifacts only if they are small and deterministic:
   - JSON summary with frame count, peak frequency, peak magnitude, sample rate, FFT size, window type, and node diagnostics.
   - Optional text/CSV spectrum bins.
   - Do not require PNG/image output in the first PR unless existing helpers make this trivial.
8. Add documentation describing how to build and run the DSP demo.
9. Identify current gaps:
   - direct DFT, not FFT library/GPU FFT;
   - CPU-only;
   - only `SpectrumSinkNode<256>` plugin exists;
   - no real-time audio input;
   - no Metal execution yet.
10. Include future follow-up boundaries:
   - real spectrogram image sink;
   - multi-frame/chirp fixture;
   - CPU-vs-Metal parity;
   - real Metal kernel or Metal Performance Shaders FFT if supported;
   - performance instrumentation comparison.

Output:
Save the planner report to:

plan/roadmap/DSP_SPECTRUM_DEMO_PR_ROADMAP.md

Report format:
For each planned PR provide:
- title
- purpose
- files to touch
- files to delete
- tests to add
- tests to delete
- acceptance criteria
- risks
- rollback plan
- whether it is CI-safe or local-only

Suggested PR shape:
- PR1: CPU DSP graph demo config and integration test.
- PR2: Demo executable or runner plus deterministic JSON/spectrum output.
- PR3: Documentation and guardrails for CPU-only/non-GPU truth-in-labeling.
- Future, out of scope: Metal DSP implementation and spectrogram image sink.
```


================

Recommended improvements:

1. **Rename / split the CPU FFT path**
   The current `FFTNode` is really a CPU spectrum node using `FFTManager`, whose comment says it uses a direct DFT for `N <= 1024`, not a true FFT. Rename it to `CpuSpectrumDftNode` or replace the implementation with an actual radix-2 FFT. Otherwise it conflicts conceptually with `MetalSpectrumDftNode`, which is explicitly `direct_dft`.  

2. **Unify CPU and Metal output semantics**
   `MetalSpectrumDftNode` always emits `MagnitudePacket` with `num_accumulated_packets = 1`, while the CPU path supports accumulation through `FFTManager`. Either add accumulation to the GPU path or make accumulation a separate upstream node so CPU/GPU spectrum nodes are comparable.  

3. **Do not hard-code Hann inside the Metal kernel**
   The Metal DFT kernel embeds Hann windowing directly in generated source. Move window type into config and either generate the proper kernel source or pass precomputed window coefficients as a device buffer. 

4. **Fix diagnostics**
   `FFTNode::GetDiagnostics()` returns an empty object, while the Metal/H2D/D2H nodes expose useful state such as backend, bytes, tickets, peak bin, and algorithm. Add diagnostics for sample rate, window type, accumulation count, packet counters, FFT/DFT count, average compute time, peak frequency history, and last output validity.  

5. **Make config validation consistent**
   `FFTNode::Configure()` silently ignores invalid `accumulation_count <= 0` and unknown window names, while `ConfigureExpected()` rejects them. Use one validation policy and make invalid configuration fail loudly. 

6. **Catch sidecar type errors in `FFTNode`**
   The GPU DFT path catches `std::bad_cast` when extracting the IQ sidecar. `FFTNode` directly calls `packet.sidecar.template get<IqPacketType>()`. Add the same guarded extraction so malformed tokens return `nullopt` instead of throwing through the graph.  

7. **Add algorithm selection**
   Introduce a common node parameter:
   `algorithm = direct_dft | radix2_fft | metal_dft | metal_fft`
   Then use a common spectrum interface so tests can compare CPU DFT, CPU FFT, and Metal DFT from the same IQ input.

8. **Improve GPU execution model**
   The Metal DFT launches one thread per bin with one serial loop over all samples. That is fine for validation, but should be labeled reference-quality. For performance, add a real Metal FFT path using staged radix-2 butterflies, shared/threadgroup memory where possible, and separate magnitude extraction.

9. **Separate “complex spectrum” from “magnitude spectrum”**
   Both CPU and Metal paths collapse directly to magnitudes. For SAR, phase matters. Add an intermediate `ComplexSpectrumPacket` / complex device layout, then make magnitude a downstream optional node.

10. **Fix D2H synchronization semantics**
    `DspMagnitudeD2HNode` waits on the kernel event before enqueueing D2H. Prefer preserving async graph semantics by having D2H depend on the kernel ticket/event instead of host-blocking, unless this node is explicitly a synchronization boundary. 

Highest-priority PR order:

**PR1:** Rename/document CPU DFT vs FFT and fix config/error handling.
**PR2:** Add useful diagnostics and metrics.
**PR3:** Unify CPU/GPU spectrum semantics and tests.
**PR4:** Add complex spectrum output.
**PR5:** Add real FFT backend after correctness is locked.
