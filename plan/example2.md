Use this prompt:

```text
You are writing a reviewer-ready implementation plan for a new GraphX SDR/SAR example.

Mission:
Produce a detailed but PR1-scoped plan for a synthetic SAR graph pipeline that demonstrates GraphX architecture patterns (plugin-backed nodes, JSON topology loading, CPU graph orchestration, GPU-style async transfer/kernel stages, fan-out/fan-in composition, and diagnostics).

Required location:
The example must be implemented under GraphX/examples/SAR. Treat examples/SAR as the package boundary for example-specific nodes, topology JSON, fixtures, docs, and executable/demo entry points. Only move code into libgraph/libdsp/libgpu when the plan makes an explicit reusable-library decision with justification.

Hard constraints:
- Assume you have access to the current repository and MUST inspect existing tests/patterns before proposing changes.
- The first PR must be architecture-correct and testable, not mathematically perfect SAR.
- No real tactical waveform decoding or operational signal references.
- Use synthetic IQ data and synthetic platform geometry only.
- Keep PR1 small: no more than 4 new nodes.
- Reuse existing async transfer/device node patterns unless impossible (justify any new GPU-style node).
- Do not duplicate the existing vibration-health pipeline example; explicitly explain how SAR differs.
- Require deterministic synthetic data, deterministic packet/tile counts, and CI-stable diagnostics tolerances.
- Keep plugin loading behind NodeProviderBootstrap and expose only the INodeProvider/registered-provider contract to graph construction code.
- Use JSON topology loading as the primary example path; programmatic graph construction may only support focused tests or helper setup.
- Make graph-level CPU parallelism visible through branches, tiles, or fan-out/fan-in stages rather than hiding the decomposition inside one node.
- Preserve explicit async movement boundaries: host ingress/staging, H2D transfer, device/kernel stage, D2H transfer, and host egress/merge.
- Define host buffer, device lease, and image tile ownership at every graph edge.
- Provide a CI-stable fallback path for machines without native GPU support, using existing simulated/stub backend behavior where appropriate.

Repository baseline references you must anchor to:
- libgraph/test/unit/test_sdr_graph.cpp
- libgraph/test/unit/test_json_dynamic_graph_loader.cpp
- libgraph/test/unit/test_plugin_system.cpp
- libgpu/test/unit/test_gpu_topologies.cpp
- libgpu/test/runtime/test_metal_runtime_graph_pipeline.cpp

Preferred SAR shape:
SyntheticApertureIqSource
-> RangeWindow / RangeCompression
-> parallel Range FFT or pulse-tile branches
-> AzimuthTileSplit
-> async H2D transfer
-> device backprojection or matched-filter stage (simulated is acceptable in PR1)
-> async D2H transfer
-> ImageTileMerge
-> Detection/Metrics sink

Required deliverables (in this exact order):
1. Recommended PR1 scope (explicitly bounded)
2. Proposed file layout
3. Node list and responsibilities
4. Message/buffer types needed
5. JSON topology shape
6. Execution and diagnostics flow
7. Unit/integration tests to add
8. Build/CMake/plugin registration changes
9. Risks and staged follow-up PRs
10. Acceptance criteria

Architectural decisions that must be made explicitly:
1. SAR mode: stripmap vs spotlight for PR1.
2. Processing granularity: pulse packet, range tile, azimuth tile, image tile, or another unit.
3. Parallelization boundary: graph branches vs internal worker pools.
4. GPU boundary: reuse generic GPU transfer/device nodes vs add SAR-specific device node(s).
5. Data ownership: host buffers, device leases, tile IDs, sequence IDs, and final image ownership.
6. Message schema: payload types, metadata fields, completion signal semantics, and diagnostics payloads.
7. Math fidelity: deterministic simulated transform in PR1 vs real range compression/backprojection in later PRs.
8. Backend policy: native Metal/CUDA/SYCL support vs stub/simulated fallback in CI.
9. Example/library boundary: examples/SAR-only code vs promoted reusable libdsp/libgpu/libgraph code.
10. Build policy: whether examples/SAR builds by default, behind an option, or only in targeted tests.

Additional output requirements:
- For each proposed node, include:
  - reused vs new
  - concrete file path(s)
  - message contract
  - tests in PR1
  - deferred follow-up work
- Include an explicit PR1 Non-Goals section.
- Include a PR2/PR3 roadmap section.
- Include estimated complexity/risk per deliverable (Low/Medium/High).
- Keep the plan reviewable as one first PR.
- Include a decision log table with columns: decision, selected option, rejected alternatives, reason, and follow-up risk.
- Include examples/SAR in the file layout, including expected subdirectories such as src, include, plugins, config, test, and doc if they are needed.
```
