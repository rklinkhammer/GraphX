
Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Create a PR-sized implementation roadmap for adding a real GPU-backed DSP spectrum/FFT lane to GraphX.

Context:
The current CPU DSP demo shape is:

SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>

The DSP node boundaries have been moved to use:

graph::gpu::accel::ControlToken

Current CPU behavior must remain as the correctness reference. The GPU lane must make host/device movement explicit in the graph and must not pretend CPU work is GPU work.

Goal:
Add a truthful GPU DSP graph lane shaped like:

SineSignalNode<256>
  -> DspIqH2DNode<256>
  -> Metal DSP transform node
  -> DspMagnitudeD2HNode<256>
  -> SpectrumSinkNode<float, 256>

The Metal transform must execute a real Metal kernel or fail clearly. It must not call the CPU FFTManager and then decorate the token with GPU metadata.

Planning rules:
- Do not implement code.
- Do not remove the existing CPU FFTNode path.
- Do not rename the CPU FFTNode.
- Preserve the CPU DSP graph as the reference lane.
- Add GPU work as a separate explicit graph lane.
- Prefer small, independently compiling PRs.
- Each PR must include tests.
- No compatibility shims.
- No fake GPU execution.
- No placeholder Metal nodes claiming algorithm support.
- If the first GPU algorithm is a direct DFT rather than FFT, name it truthfully, for example MetalSpectrumDftNode, not MetalFFTNode.
- Keep GraphX graph builder, executor, plugin loading, JSON config, and completion conventions.
- Use the existing libgpu Metal capability model where appropriate.
- Use the existing SAR H2D/D2H token-preserving pattern only as a structural reference; do not introduce SAR concepts into DSP.
- Keep DSP types generic and free of SAR/GOTCHA/CRSD naming.
- CI must not require special external datasets or audio devices.

Required analysis:
1. Inspect the current DSP nodes:
   - libdsp/include/dsp/SineSignalNode.hpp
   - libdsp/include/dsp/FFTNode.hpp
   - libdsp/include/dsp/SpectrumSinkNode.hpp
   - libdsp/src/dsp/FFTNode.cpp
   - libdsp/src/dsp/SpectrumSinkNode.cpp
   - libdsp/config/dsp_sine_fft_spectrum_256.json

2. Inspect existing GPU transfer and Metal node patterns:
   - libgpu/include/gpu/accel/types/AccelTypes.hpp
   - libgpu/include/gpu/metal/nodes/H2DAsyncNodeMetal.hpp
   - libgpu/include/gpu/metal/nodes/D2HAsyncNodeMetal.hpp
   - libgpu/include/gpu/metal/nodes/DeviceTransformNodeMetal.hpp
   - libgpu/include/gpu/metal/capabilities/IMetalCapabilities.hpp
   - libgpu/include/gpu/metal/capabilities/NativeMetalCapabilities.hpp
   - examples/SAR/src/H2DAsyncAccelNode.cpp
   - examples/SAR/src/D2HAsyncAccelNode.cpp

3. Inspect existing DSP demo/runtime tests:
   - libgraph/test/unit/test_sdr_graph.cpp
   - libgraph/test/unit/test_dsp_spectrum_graph_runtime.cpp
   - examples/DSP/test/test_dsp_spectrum_demo.cpp

Architecture requirements:
- Define explicit DSP H2D and D2H nodes that preserve ControlToken sidecars.
- DspIqH2DNode must serialize IqPacket<float, 256> into a Metal-compatible device buffer layout.
- The Metal transform node must consume a device-backed token and produce a device-backed magnitude token.
- DspMagnitudeD2HNode must reconstruct MagnitudePacket<float, 256>.
- SpectrumSinkNode must remain the final host-side sink.
- CPU FFTNode remains the reference implementation.
- GPU output must be compared against CPU output within deterministic tolerance.
- The GPU transform must expose diagnostics proving real kernel launch:
  - valid kernel ticket
  - valid device input/output views
  - backend == Metal where available
  - no CPU FFTManager processing path

Preferred first implementation:
Use a real Metal direct DFT kernel for N=256 if implementing a full FFT is too large for the first GPU PR.

If direct DFT is selected:
- Name the node MetalSpectrumDftNode<256> or similar.
- Document that it is a real GPU DFT spectrum transform, not an optimized FFT.
- Leave a later PR boundary for a true Metal FFT implementation.

Required roadmap coverage:
1. DSP token transfer model.
2. DSP H2D node.
3. DSP GPU spectrum transform node.
4. DSP D2H node.
5. GPU DSP JSON config.
6. Plugin registration.
7. Executor integration test.
8. CPU-vs-GPU numerical comparison.
9. Truth-in-labeling guardrails.
10. Documentation updates.
11. Future true FFT boundary.

Output:
Save the planner report to:

plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md

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

PR1: DSP GPU Token Transfer Contracts
- Add/verify DSP-specific token invariants and byte-layout documentation.
- Define how IqPacket<float, 256> maps to host/device buffers.
- Define how MagnitudePacket<float, 256> is reconstructed.
- Add focused tests for token sidecar preservation expectations.
- No Metal kernel yet.

PR2: DSP IQ H2D Node
- Add DspIqH2DNode<256>.
- Preserve ControlToken<Message> sidecar.
- Populate host/device view metadata correctly.
- Add plugin and focused tests.
- Do not add GPU transform yet.

PR3: Real Metal Spectrum DFT Node
- Add a Metal-backed spectrum transform node for N=256.
- Must launch a real Metal kernel.
- Must not call FFTManager.
- Produces device-backed magnitude token.
- Add kernel-ticket and diagnostics tests.
- If it is DFT, name it DFT, not FFT.

PR4: DSP Magnitude D2H Node
- Add DspMagnitudeD2HNode<256>.
- Reconstruct MagnitudePacket<float, 256>.
- Preserve token metadata.
- Compute or verify peak frequency/magnitude on host after copy-back.
- Add focused tests.

PR5: GPU DSP Graph Config And Executor Integration
- Add JSON config:
  SineSignalNode<256> -> DspIqH2DNode<256> -> MetalSpectrumDftNode<256> -> DspMagnitudeD2HNode<256> -> SpectrumSinkNode<256>
- Add graph-builder/executor integration test.
- Verify completion.
- Verify SpectrumSinkNode receives at least one frame.
- Verify real GPU diagnostics are present.

PR6: CPU-vs-GPU Spectrum Parity
- Run CPU reference lane and GPU lane on the same deterministic sine input.
- Compare peak frequency, peak magnitude, and selected magnitude bins.
- Define deterministic tolerance.
- Add CI-safe tests with Metal available.
- If Metal is unavailable, test must skip clearly rather than fake success.

PR7: Truth-In-Labeling And Documentation
- Document CPU lane vs GPU lane.
- State whether the first GPU transform is DFT or FFT.
- Add guardrail tests preventing GPU-labeled DSP nodes from calling FFTManager or reporting GPU success without a kernel ticket.
- Update DSP README/examples.

Future out of scope:
- True Metal FFT implementation.
- MPS or platform FFT exploration.
- Multi-frame spectrogram image sink.
- CPU-vs-Metal performance instrumentation.
- Streaming audio input.

Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Create a PR-sized roadmap for comparing CPU DSP graph performance against Metal DSP graph performance in a Metal-enabled build.

Goal:
It should be possible to run a CPU-based DSP graph and a Metal-enabled DSP graph under the same GraphX runtime and compare executor timing values from `GraphExecutor` return/results to validate whether the Metal lane provides a measurable performance improvement.

Inputs:
- plan/roadmap/DSP_GPU_SPECTRUM_PR_ROADMAP.md
- plan/agents/DSP_GPU_SPECTRUM_PR_AGENTS.md
- docs/dsp/spectrum_demo.md
- libdsp/config/dsp_sine_fft_spectrum_256.json
- libdsp/config/dsp_sine_metal_dft_spectrum_256.json
- libgraph/test/unit/test_dsp_gpu_spectrum_parity.cpp
- libgraph/test/unit/test_dsp_gpu_spectrum_graph_runtime.cpp
- examples/DSP/src/main.cpp
- Current repository state

Planning requirements:
- Do not implement code.
- Do not redesign GraphX runtime contracts.
- Do not rename `MetalSpectrumDftNode` to FFT.
- Do not claim Metal is faster unless measured timings show it.
- Preserve CPU DSP lane as the correctness reference.
- Preserve GPU DSP lane as explicit:
  `SineSignalNode<256> -> DspIqH2DNode<256> -> MetalSpectrumDftNode<256> -> DspMagnitudeD2HNode<256> -> SpectrumSinkNode<256>`
- Use existing GraphBuilder, GraphExecutor, JSON config, plugin loading, executor completion, and timing/result conventions.
- Timing comparison must use executor result timing fields where available.
- Include warm-up/iteration rules to reduce one-time plugin loading, graph construction, and Metal kernel setup noise.
- Keep CI-safe behavior: performance comparison should skip clearly when native Metal is unavailable and should not fail CI on “not faster”.
- Add optional local-only strict performance gate only if explicitly enabled.
- Avoid performance claims in docs without recorded measurement context.
- Do not add external benchmark dependencies.
- Do not require real data, audio devices, SAR, GOTCHA, CRSD, MATLAB, SarPy, or external datasets.
- Do not add spectrogram image output.
- Do not implement true Metal FFT.

Required planning coverage:
1. Identify current executor timing fields and whether they represent build time, init time, run time, total wall time, or completion wait time.
2. Define a CPU-vs-Metal DSP benchmark harness using existing CPU and GPU configs.
3. Define deterministic run controls:
   - same sine settings;
   - same FFT/DFT size;
   - same executor timeout;
   - same plugin directory;
   - warm-up iterations;
   - measured iterations;
   - repeated run summary statistics.
4. Define output artifact schema:
   - cpu_config_path;
   - gpu_config_path;
   - build preset / binary path;
   - native Metal availability diagnostics;
   - iteration timings;
   - min/median/mean/stddev;
   - speedup ratio;
   - correctness/parity summary;
   - whether the run is informational or gate-enforced.
5. Define tests:
   - schema/unit tests for benchmark report JSON;
   - harness tests using deterministic fixture/config;
   - skip behavior when Metal is unavailable;
   - guardrail that default CI does not fail only because Metal is slower;
   - optional local-only strict gate test enabled by an environment variable.
6. Define docs updates:
   - how to run the CPU-vs-Metal comparison;
   - how to interpret timing values;
   - warning that current GPU lane is direct DFT, not FFT;
   - warning that first-run timings include setup noise unless warm-up is used.
7. Define truth-in-labeling guardrails:
   - performance docs must say “measured on this host/config”;
   - docs must not imply general GPU superiority;
   - Metal DFT must not be described as GPU FFT.
8. Define whether this belongs in:
   - `examples/DSP` runner;
   - a new benchmark executable;
   - libgraph tests;
   - docs only.
9. Include a final PR that verifies all active docs/tests avoid unqualified performance claims.

Output:
Save the planner report to:

plan/roadmap/DSP_CPU_VS_METAL_PERFORMANCE_PR_ROADMAP.md

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