Use this prompt:

```text
Create a detailed implementation plan for adding a GraphX SDR/SAR example that demonstrates complex graph-based signal processing.

Context:
The project is GraphX, a C++ graph-processing architecture with plugin-backed nodes, JSON-loaded topologies, CPU graph execution, DSP nodes, and GPU-style async transfer/kernel nodes. Existing pieces include:
- DSP IQ packet and FFT/spectrum pipeline examples
- Plugin-backed node creation through RegisteredNodeProvider and NodeProviderBootstrap
- JSON topology loading
- GPU-style nodes such as HostIngressPinnedSource, H2DAsync, DeviceTransform, DeviceReduce, D2HAsync, and HostEgressSink
- Tests for SDR graph execution and GPU topology execution

Placement:
The example must live under GraphX/examples/SAR. Treat this as an example application/package boundary, not as another libgraph/libdsp/libgpu test-only fixture. Any reusable primitives that genuinely belong in libdsp/libgpu/libgraph must be called out separately and justified.

Goal:
Design a publishable SDR example using synthetic SAR image formation, not Link 16. The example should demonstrate:
- Parallel decomposition across multiple CPU graph branches
- Asynchronous data movement between host and device stages
- Buffer/tile ownership across kernel-level processing
- Fan-out/fan-in graph architecture
- Measurable diagnostics such as pulses processed, bytes moved, tiles processed, kernel dispatches, and end-to-end latency

Preferred example:
A simulated stripmap or spotlight SAR pipeline:
SyntheticApertureIqSource
→ RangeWindow / RangeCompression
→ parallel Range FFT or pulse-tile branches
→ AzimuthTileSplit
→ async H2D transfer
→ device backprojection or matched-filter kernel stage
→ async D2H transfer
→ ImageTileMerge
→ Detection/Metrics sink

Important constraints:
- First PR should be architecture-correct and testable, not mathematically perfect SAR.
- Use synthetic IQ data and synthetic platform geometry.
- Avoid implementing or referencing real tactical waveform decoding.
- Prefer existing GraphX patterns, plugin registration, JSON topologies, and test infrastructure.
- Keep plugin loading behind NodeProviderBootstrap and expose only INodeProvider/registered provider contracts to graph construction code.
- Keep SAR-specific nodes, topology JSON, fixtures, and executable entry points under examples/SAR unless there is a deliberate reusable-library decision.
- Use JSON-loaded topology as the main demonstration path; programmatic graph construction can be used only as a focused test/helper path.
- Model data movement with explicit message/buffer ownership: host packet/tile messages, device lease/buffer metadata, H2D/D2H transfer boundaries, and completion/diagnostics messages.
- Make CPU parallel decomposition visible in the topology through separate branches/tiles, not hidden inside one monolithic node.
- Define the backend behavior for machines without native GPU support: simulated/stub GPU lane is acceptable for PR1, but the plan must explain how native CUDA/SYCL/Metal paths would replace it.
- Keep the first implementation slice small enough for one reviewable PR.
- Identify which nodes should be new, which existing nodes can be reused, and which math can initially be simulated.
- Include test strategy, diagnostics strategy, and performance-measurement hooks.

Architectural decisions to make explicit:
1. SAR mode for PR1: stripmap vs spotlight, and why.
2. Data granularity: pulse packet, range tile, azimuth tile, image tile, or another unit.
3. Tile ownership: who owns host buffers, device leases, and final image tiles at each graph boundary.
4. Parallelization boundary: graph-level branch parallelism vs internal node thread pools.
5. GPU boundary: reuse existing H2D/device/D2H nodes vs introduce SAR-specific device node(s).
6. Message contract: exact payload types, metadata fields, sequence/tile IDs, timestamps, and completion behavior.
7. Diagnostics contract: deterministic counters and tolerances suitable for CI.
8. Example boundary: what belongs in examples/SAR vs reusable libdsp/libgpu/libgraph code.
9. Mathematical fidelity: simulated deterministic transforms in PR1 vs real SAR math deferred to later PRs.
10. Build integration: whether examples/SAR builds by default, behind an option, or only in tests.

Recommended implementation strategies for key algorithms:
1. Synthetic raw-data generation:
   - Use a deterministic point-scatterer scene and synthetic platform path.
   - Generate complex baseband IQ pulse/range samples with fixed seed, fixed pulse count, fixed samples-per-pulse, and explicit sample-rate/carrier/chirp metadata.
   - Keep the generator simple enough for CI: one to three point targets, small aperture, and bounded packet counts.
   - Emit sequence IDs and geometry metadata so downstream nodes can verify ordering and tile coverage.
2. Range compression / matched filtering:
   - For PR1, implement deterministic matched filtering as either direct complex convolution or FFT-based convolution over small fixed windows.
   - Prefer an FFT-based path if it can reuse existing DSP FFT infrastructure; otherwise document why a direct convolution is better for the first vertical slice.
   - Make the matched-filter reference chirp explicit and deterministic.
   - Record per-pulse processing time, input samples, output samples, and effective samples/second.
3. Windowing and FFT:
   - Reuse existing DSP window/FFT patterns where possible.
   - Keep FFT size fixed for PR1 so tests can use stable tolerances and known output sizes.
   - Include a plan for later replacement with FFTW, vDSP, or backend-specific FFT acceleration only if it belongs outside examples/SAR.
4. Image formation:
   - Use backprojection as the conceptual algorithm because it maps clearly to image tiles and GPU kernels.
   - In PR1, a deterministic simulated backprojection kernel is acceptable if it preserves the same buffer shape, tile metadata, and accumulation semantics that real backprojection would need.
   - Defer high-fidelity range interpolation, motion compensation, polar formatting, autofocus, and radiometric calibration to PR2/PR3.
5. Parallel decomposition:
   - Split work by pulse block, range tile, azimuth tile, or image tile and represent the split with graph branches.
   - Avoid hiding the primary parallelism inside one node; internal threading can be a later optimization.
   - Include a fan-in merge stage that validates all expected tile IDs arrived exactly once.
6. GPU/device processing:
   - Reuse existing async H2D/device/D2H node patterns for PR1.
   - If a SAR-specific device stage is needed, make it operate on a generic tile buffer plus metadata instead of a monolithic SAR object.
   - Preserve explicit kernel descriptor metadata, bytes transferred, dispatch count, device ID/backend, and synchronization points.
7. Baseline non-graph implementation:
   - Implement or require a single-process non-graph baseline that runs the same synthetic dataset and same algorithmic steps without GraphX scheduling, plugin dispatch, or graph message queues.
   - Use the same data sizes, same deterministic scene, same math path, and same backend mode as the graph version.
   - Report graph overhead as absolute time and percentage overhead for end-to-end runtime and for stage-level timings where comparable.

Performance and benchmark requirements:
1. Include performance measurements for key DSP metrics:
   - IQ samples generated per second
   - pulses processed per second
   - range-compression time per pulse/tile
   - FFT/window time per pulse/tile if FFT is used
   - image-tile accumulation time
   - CPU time per graph node and end-to-end latency
   - queue depth/backpressure observations where available
2. Include performance measurements for key GPU metrics:
   - H2D bytes, D2H bytes, and effective transfer bandwidth
   - kernel dispatch count
   - kernel execution time or simulated-kernel stage time
   - device-buffer allocation/reuse count
   - synchronization/wait time
   - device occupancy/proxy utilization only if a native backend can report it reliably
3. Include graph-overhead comparison:
   - Compare GraphX implementation against a non-graph baseline implementation of the same SAR algorithm.
   - Report wall-clock end-to-end time, per-stage time, throughput, and memory-transfer totals for both paths.
   - Attribute overhead categories: graph scheduling, message allocation/copy, queueing/backpressure, plugin/provider lookup, and diagnostics collection.
   - Require warm-up runs and repeated measured runs; report median, min/max, and standard deviation or a simpler CI-stable summary.
   - Make benchmark sizes configurable but include a small CI-safe profile and a larger local profile.
4. Benchmark correctness constraints:
   - The graph and non-graph baseline must consume identical deterministic synthetic input.
   - The final image/tile output must match within explicit tolerances.
   - Performance tests should not be brittle CI gates unless thresholds are relative and conservative; correctness and metric presence should be CI-gated.

Algorithm and measurement references to consult:
1. SAR image-formation survey: "A Review of Synthetic-Aperture Radar Image Formation Algorithms and Implementations: A Computational Perspective" (Remote Sensing, 2022).
2. SAR fundamentals/tutorial: DLR/IEEE GRSS SAR tutorial PDF.
3. Backprojection background: Sandia-style backprojection/image-formation reports and filtered backprojection introductions.
4. FFT implementation/performance background: FFTW manual and FFTW3 design paper for planner/cache/SIMD considerations.
5. GPU timing/bandwidth measurement: CUDA events and CUDA best-practices guidance for elapsed time and effective bandwidth.

Deliverables requested:
1. Recommended first PR scope
2. Proposed file layout
3. Node list and responsibilities
4. Message/buffer types needed
5. JSON topology shape
6. Execution and diagnostics flow
7. Unit/integration tests to add
8. Build/CMake/plugin registration changes
9. Risks and staged follow-up PRs
10. Clear acceptance criteria

Also require:
1. Assume you have access to the current repository. Inspect existing test coverage before proposing changes.
2. Ground every recommendation in existing patterns from these files:
	- libgraph/test/unit/test_sdr_graph.cpp
	- libgraph/test/unit/test_json_dynamic_graph_loader.cpp
	- libgraph/test/unit/test_plugin_system.cpp
	- libgpu/test/unit/test_gpu_topologies.cpp
	- libgpu/test/runtime/test_metal_runtime_graph_pipeline.cpp
3. For PR1, limit implementation scope to a minimal vertical slice with no more than 4 new nodes.
4. Reuse existing GPU async transfer/device node patterns unless impossible; justify any new GPU-style node.
5. Do not duplicate the existing vibration-health pipeline example; explicitly explain how SAR differs.
6. Require deterministic synthetic data, deterministic packet/tile counts, and CI-stable diagnostics/tolerances.
7. For each proposed node, include: reused/new status, file path, message contract, tests, and deferred follow-ups.
8. Include explicit non-goals for PR1 and what is deferred to PR2/PR3.
9. Place all example-specific files under examples/SAR and include that directory in the proposed file layout.
10. Include a decision log table with: decision, selected option, rejected alternatives, reason, and follow-up risk.
11. Include a benchmark plan comparing GraphX against a non-graph baseline for the same synthetic SAR algorithm and dataset.
12. Include required DSP/GPU performance metrics, measurement methodology, CI-safe profile, and local larger-profile benchmark.
13. Include references for selected SAR image-formation algorithm, DSP/FFT approach, and GPU timing methodology.
```
