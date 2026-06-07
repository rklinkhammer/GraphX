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

Additional GraphX architectural constraints from external review:
All 17 external-review considerations are relevant to the SAR example prompt, but not all should be implemented in PR1. Classify each item as PR1 requirement, PR1 decision, or deferred follow-up. Do not blindly introduce new framework concepts if GraphX already has an equivalent pattern.

1. Control plane vs data plane:
   - Make explicit that graph edges carry tokens/context/metadata, not implicit byte movement.
   - Actual byte movement belongs to capabilities or backend-specific nodes.
   - Preserve this separation: Edges carry tokens and context; Nodes perform transformations; Capabilities execute backend operations; Leases represent ownership; Tickets represent asynchronous completion.
2. Sequence and watermark contracts:
   - Define sequence/tile metadata for pulse, range tile, azimuth tile, image tile, timestamp, and optional backend/device fields.
   - Specify ordering guarantees, duplicate detection, missing tile behavior, late tile handling, and end-of-stream watermarks.
3. Explicit end-of-stream:
   - Prefer explicit completion/end-of-stream messages over nullptrs, queue exhaustion, or magic counters.
   - Use existing GraphX completion signal patterns where possible; propose new SAR-specific EOS types only if required.
4. Backpressure diagnostics:
   - Include queue depth, queue high-water marks, producer stalls, consumer stalls, dropped packets, and latency percentile reporting where GraphX exposes the required data.
5. Thread ownership model:
   - Nodes should not own thread pools in PR1.
   - Parallelism should come from graph branches and executor workers; internal node worker pools are deferred.
6. Backend-neutral kernel dispatch metadata:
   - Require a common diagnostic view of backend, device ID, queue/stream ID, dispatch ID, grid/block/threadgroup dimensions where applicable, input bytes, output bytes, and completion event/ticket.
   - Reuse existing kernel descriptor/dispatch metadata first; introduce new structs only with justification.
7. Buffer lifetime state:
   - Define the intended buffer lifecycle for SAR tiles: Allocated, HostFilled, ReadyForTransfer, TransferInFlight, DeviceReady, KernelRunning, KernelComplete, TransferBack, HostReady, Consumed, Released.
   - PR1 may track this as diagnostics/state labels rather than a full framework-level state machine.
8. Lease reuse metrics:
   - Measure allocation count, peak allocations, reuse count, bytes reused, and release count where existing allocators/capabilities expose the data.
9. Execution trace recording:
   - Include an optional trace plan for node name, sequence ID, timestamp, phase, backend, and tile ID.
   - Chrome trace, Perfetto, CSV, or JSON export can be deferred unless a lightweight JSON/CSV trace already fits PR1.
10. Error/status contract:
   - Prefer typed execution status that includes success, node name, sequence/tile ID, error category, and message.
   - Align with existing GraphX result/status types before proposing new error contracts.
11. Deterministic scheduler/debug mode:
   - Require a deterministic debug profile for golden-output generation, either by using existing single-thread/controlled execution behavior or by proposing a small test-only mode.
   - Do not make a new scheduler a PR1 requirement unless the current executor cannot produce deterministic tests.
12. Graph overhead attribution:
   - Separate algorithm time, scheduling time, message allocation/copy overhead, queue wait time, provider/plugin lookup overhead, and diagnostics overhead.
13. Tile merge correctness:
   - Merge stages must verify expected tile count, unique tile IDs, no duplicates, all pulse/range coverage represented, and explicit completion before emitting a final image.
14. Simulated device backend:
   - Audit existing stub/simulated GPU backend behavior before proposing BackendKind::Simulated.
   - PR1 must have a CI-stable backend path that does not require native GPU availability.
15. Multi-device future metadata:
   - Include device_id, queue_id, stream_id, numa_domain, and backend fields in metadata where they are cheap and non-invasive, even if PR1 uses one device/backend.
16. SAR as a GraphX demonstration:
   - The example is primarily a GraphX architecture demonstration: fan-out/fan-in, plugin loading, ownership, async execution, device boundaries, diagnostics, and benchmark comparison.
   - SAR mathematical fidelity is secondary in PR1.
17. DAG event/ticket model:
   - Identify whether existing transfer/kernel/completion ticket concepts already exist.
   - If not, specify the minimal PR1 metadata needed and defer broader DAG event modeling.

Additional DAG decomposition guidance from follow-up review:
The prompt has enough general GraphX abstraction guidance. From here, prioritize DSP decomposition, GPU execution semantics, DAG behavior/correctness, measurement/scalability, and long-term heterogeneous execution.

1. Separate the DSP DAG from the device DAG:
   - Treat the DSP DAG and device DAG as conceptually independent layers.
   - DSP DAG concepts: pulses, FFT blocks, range tiles, azimuth tiles, image fragments.
   - Device DAG concepts: leases, transfer tickets, dispatch metadata, completion events.
   - Avoid monolithic message types that mix all DSP and device concerns into one object.
2. Preserve tile independence:
   - PR1 should maximize independent tile processing.
   - Avoid a global mutable SAR image object traveling through the graph.
   - Every tile should carry tile_id, range_block, azimuth_block, pulse_range, sequence_number, batch_id, tile_count, backend, device_id, and queue_id where applicable.
   - Tile independence should support out-of-order completion, backend scheduling, and future multi-device execution.
3. Treat ImageTileMerge as a core demonstration node:
   - ImageTileMerge is the most important correctness node in PR1.
   - It must validate expected tile count, unique tile IDs, missing tiles, duplicate tiles, EOS/watermark handling, and fan-in wait behavior before emitting a completed image/tile result.
   - The plan should explain how this node could later support partial image preview, sliding apertures, and streaming SAR.
4. Prefer backprojection for the GraphX demonstration:
   - Backprojection maps better to GraphX than range-Doppler for PR1 because image tiles are embarrassingly parallel, natural GPU kernel units, and easy to fan in.
   - PR1 should favor slow but deterministic backprojection semantics over high-performance or high-fidelity SAR math.
5. Kernel granularity matters more than algorithm fidelity:
   - Define one kernel work unit explicitly, preferably one image tile.
   - A candidate PR1 tile size can be 256x256 for local runs, with a smaller CI-safe profile if needed.
   - Kernel input should be pulse block data, geometry metadata, and tile coordinates.
   - Kernel output should be a complex image tile or deterministic simulated equivalent.
6. Keep DeviceTransformNode generic:
   - Do not put SarBackprojectionNode into libgpu for PR1.
   - Prefer existing generic DeviceTransformNode/DeviceReduceNode patterns with kernel descriptors or operation metadata for backprojection.
   - SAR-specific knowledge belongs in examples/SAR unless a reusable abstraction is explicitly justified.
7. Consider DeviceReduceNode as a major showcase:
   - Backprojection accumulation can naturally demonstrate tile shards flowing through DeviceReduce into merged image tiles.
   - The plan should decide whether PR1 needs DeviceReduce or whether it is a stronger PR2 follow-up.
8. Model DAG width and depth:
   - SAR should demonstrate both wide tile parallelism and deep staged processing.
   - Include at least one explicit wide decomposition in PR1, and describe how deeper Window -> FFT -> Compression -> Backprojection -> Reduce stages evolve in PR2/PR3.
9. Do not assume one backend:
   - Metadata should not bake in a single backend.
   - Future scenarios may combine CPU FFT, CUDA/SYCL/Metal backprojection, Metal visualization, or heterogeneous tile dispatch.
10. Defer dynamic load balancing:
   - Work stealing, adaptive scheduling, multi-GPU routing, and heterogeneous dispatch are natural PR4/PR5 topics, not PR1.
   - PR1 should preserve the metadata and tile independence needed for those later capabilities.

Architectural decisions to make explicit:
1. SAR mode for PR1: stripmap vs spotlight, and why.
2. Data granularity: pulse packet, range tile, azimuth tile, image tile, or another unit.
3. Tile ownership: who owns host buffers, device leases, and final image tiles at each graph boundary.
4. Parallelization boundary: graph-level branch parallelism vs internal node thread pools.
5. GPU boundary: reuse existing H2D/device/D2H nodes vs introduce SAR-specific device node(s).
6. Message contract: exact payload types, metadata fields, sequence/tile IDs, timestamps, end-of-stream/watermark behavior, and completion behavior.
7. Diagnostics contract: deterministic counters and tolerances suitable for CI.
8. Example boundary: what belongs in examples/SAR vs reusable libdsp/libgpu/libgraph code.
9. Mathematical fidelity: simulated deterministic transforms in PR1 vs real SAR math deferred to later PRs.
10. Build integration: whether examples/SAR builds by default, behind an option, or only in tests.
11. Control/data-plane boundary: how graph messages represent tokens, buffer leases, tickets, and diagnostics without hiding backend byte movement.
12. Benchmark boundary: what is measured in CI vs local performance runs.
13. DAG layering: where the DSP DAG ends and the device DAG begins.
14. Tile granularity: whether PR1 uses pulse blocks, image tiles, tile shards, or another unit as the primary independent work item.
15. Merge strategy: how ImageTileMerge handles out-of-order completion, duplicate/missing tiles, EOS, watermarks, and fan-in wait time.
16. Generic device strategy: how SAR operations are represented through generic device transform/reduce nodes rather than SAR-specific libgpu nodes.

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
   - Prefer backprojection over range-Doppler for PR1 unless repository inspection reveals a much stronger existing range-Doppler path.
   - Define one image tile as the primary kernel work unit when possible.
   - In PR1, a deterministic simulated backprojection kernel is acceptable if it preserves the same buffer shape, tile metadata, and accumulation semantics that real backprojection would need.
   - Defer high-fidelity range interpolation, motion compensation, polar formatting, autofocus, and radiometric calibration to PR2/PR3.
5. Parallel decomposition:
   - Split work by pulse block, range tile, azimuth tile, or image tile and represent the split with graph branches.
   - Avoid hiding the primary parallelism inside one node; internal threading can be a later optimization.
   - Include a fan-in merge stage that validates all expected tile IDs arrived exactly once.
6. GPU/device processing:
   - Reuse existing async H2D/device/D2H node patterns for PR1.
   - If a SAR-specific device stage is needed, make it operate on a generic tile buffer plus metadata instead of a monolithic SAR object.
   - Prefer generic DeviceTransformNode and DeviceReduceNode semantics over SAR-specific libgpu node classes.
   - Decide whether DeviceReduce belongs in PR1 or should be deferred as a stronger PR2 demonstration of accumulation, synchronization, and ownership.
   - Preserve explicit kernel descriptor metadata, bytes transferred, dispatch count, device ID/backend, and synchronization points.
7. Baseline non-graph implementation:
   - Implement or require a single-process non-graph baseline that runs the same synthetic dataset and same algorithmic steps without GraphX scheduling, plugin dispatch, or graph message queues.
   - Use the same data sizes, same deterministic scene, same math path, and same backend mode as the graph version.
   - Report graph overhead as absolute time and percentage overhead for end-to-end runtime and for stage-level timings where comparable.
8. Future FFT acceleration:
   - PR1 may use small deterministic DSP kernels.
   - PR2+ should evaluate FFTW, MKL, vDSP, cuFFT, oneMKL FFT, or Metal Performance Shaders for range FFT, azimuth FFT, and matched filtering.
   - The topology and message contracts must not assume a single FFT implementation.

Performance and benchmark requirements:
1. Include performance measurements for key DSP metrics:
   - IQ samples generated per second
   - pulses processed per second
   - tiles processed per second
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
   - completion ticket/event wait time
   - device occupancy/proxy utilization only if a native backend can report it reliably
3. Include performance measurements for key DAG metrics:
   - queue depth and queue high-water marks
   - worker utilization where available
   - fan-in wait time
   - backpressure and stall time
   - out-of-order tile completion count
   - duplicate/missing tile detection count
4. Include graph-overhead comparison:
   - Compare GraphX implementation against a non-graph baseline implementation of the same SAR algorithm.
   - Report wall-clock end-to-end time, per-stage time, throughput, and memory-transfer totals for both paths.
   - Attribute overhead categories: graph scheduling, message allocation/copy, queue wait/backpressure, provider/plugin lookup, diagnostics collection, and backend synchronization.
   - Require warm-up runs and repeated measured runs; report median, min/max, and standard deviation or a simpler CI-stable summary.
   - Make benchmark sizes configurable but include a small CI-safe profile and a larger local profile.
5. Benchmark correctness constraints:
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
11. Relevance table for the 17 external-review considerations, marking each as PR1 requirement, PR1 decision, deferred, or rejected with reason.
12. Graph vs non-graph benchmark plan with overhead attribution.
13. DAG layering plan that separately describes the DSP DAG, tile DAG, transfer DAG, kernel DAG, and reduction/merge DAG.
14. ImageTileMerge correctness plan and test matrix.
15. Long-term heterogeneous execution roadmap covering multi-backend metadata, dynamic load balancing, and why those are deferred beyond PR1.

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
14. Do not propose framework-wide rewrites as PR1 scope. If an external-review issue implies broader GraphX architecture work, classify it as deferred and define the smallest PR1-compatible interface or diagnostic hook.
15. Stop adding general GraphX abstractions unless they are needed to express DSP decomposition, GPU execution semantics, DAG correctness, measurement/scalability, or long-term heterogeneous execution.
```
