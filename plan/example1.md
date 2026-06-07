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
```
