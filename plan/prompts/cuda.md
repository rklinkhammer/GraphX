Use this implementation prompt:

```text
You are working in the GraphX repository. Update the backend graph-node story by auditing the existing Metal nodes, implementing semantically meaningful CPU and native CUDA equivalents where they do not already exist, and adding correctness plus performance coverage that compares equivalent end-to-end graphs.

Work autonomously: inspect the repository, make the changes, build them, run relevant tests, and fix failures. Preserve unrelated user changes. Use the repository's existing host-toolchain conventions; do not hardcode compilers or bypass the configured CMake presets.

Scope

1. Audit the existing Metal nodes in:

   - `libgpu/include/gpu/metal/nodes`
   - `libgpu/src/gpu/metal/nodes`
   - `libgpu/plugins`
   - Metal domain nodes in `libdsp` and `examples`

2. Produce a parity matrix containing, at minimum:

   - Metal node
   - classification: compute, transfer, memory, synchronization, or control
   - existing CPU/CUDA equivalent, if any
   - missing implementation status
   - proposed CPU and CUDA node names
   - whether a CPU equivalent is meaningful
   - rationale for any intentionally GPU-specific operation

3. Implement CPU and native CUDA versions where the operation is semantically meaningful on that backend.

Implementation requirements

- Keep graph-facing contracts aligned across backends:
  - input/output token types
  - port indices
  - configuration fields
  - lifecycle and error behavior
  - plugin registration and node discovery
- Reuse backend-neutral logic instead of copying entire implementations.
- CPU nodes must execute the real algorithm on the host.
- CUDA compute nodes must execute real CUDA kernels. Do not represent CPU fallback, simulation, or token forwarding as native CUDA acceleration.
- Implement actual CUDA allocation, transfers, streams/events, synchronization, kernel launch, and error propagation where applicable.
- Preserve existing Metal behavior.
- Gate backend availability through CMake feature detection.
- Keep CPU-only builds functional.
- Skip CUDA tests with a precise reason when the native runtime or hardware is unavailable.
- Do not invent CPU counterparts for inherently GPU-specific operations such as peer-device copies. Document those cases in the parity matrix instead.
- Add plugins, configurations, CMake targets, presets, and installation rules consistent with existing repository conventions.

Correctness tests

Add tests proving:

- CPU, CUDA, and Metal implementations produce equivalent results within justified numerical tolerances.
- Dynamic plugin creation succeeds.
- Graph topology, port contracts, and token metadata agree across backends.
- Invalid configuration and runtime errors are handled consistently.
- CUDA execution is genuinely native and observable through backend diagnostics.
- Existing tests continue to pass.

Performance tests

Create a dedicated performance-test suite inspired by the graph construction and plugin-loading approach in `libdsp/test/unit/test_sdr_graph.cpp`.

Do not turn ordinary unit tests into timing assertions. Build equivalent end-to-end CPU, CUDA, and Metal graphs using the same:

- source data
- problem sizes
- algorithms
- output validation
- iteration counts
- warm-up policy

Benchmark at least:

- compute-only execution
- host-to-device and device-to-host transfer costs
- complete graph latency
- steady-state throughput
- small, medium, and large workloads
- cold-start versus warmed execution

Methodology requirements:

- Use Release builds.
- Warm each backend before collecting samples.
- Synchronize GPU work before stopping timers.
- Exclude graph/plugin initialization from steady-state measurements, but report it separately.
- Run enough iterations for stable results.
- Report sample count, median, mean, standard deviation, p95, minimum, and maximum.
- Validate outputs before accepting timing results.
- Record backend, device, workload size, build type, and relevant runtime information.
- Emit both human-readable output and machine-readable JSON or CSV.
- Never fail CI because one backend is slower on a particular machine.
- Performance assertions may detect catastrophic regressions only when a stable, documented threshold exists.
- Never claim a performance gain unless the measurements demonstrate it.
- Calculate speedup as CPU time / GPU time and clearly distinguish kernel-only, transfer-inclusive, and end-to-end graph speedup.
- If CUDA or Metal is unavailable, report that backend as unavailable rather than substituting another implementation.

Prefer a reusable benchmark harness over duplicated backend-specific tests. Add documented commands for configuring, building, and running each benchmark lane.

Acceptance criteria

- Every meaningful existing Metal node has an implemented CPU and CUDA equivalent, or a documented technical reason why it does not.
- CPU-only, CUDA-enabled, and Metal-enabled configurations build appropriately.
- Backend-parity correctness tests pass.
- Existing GraphX test lanes do not regress.
- Performance tests compare equivalent work and generate reproducible reports.
- Results clearly show where acceleration begins to outweigh transfer and launch overhead.
- Documentation accurately distinguishes compute acceleration from transfer, memory, synchronization, and control nodes.
- No simulated or fallback path is labeled as native CUDA performance.

Execution approach

1. Inspect the repository and create the parity matrix.
2. Identify reusable contracts and shared implementation components.
3. Implement in small, independently buildable increments.
4. Add correctness tests before performance comparisons.
5. Add and run the benchmark suite.
6. Review names and documentation for truthful backend labeling.
7. Finish with:
   - files changed
   - parity matrix
   - build/test commands executed
   - test results
   - benchmark methodology
   - measured results, if supported hardware is available
   - remaining limitations

Do not stop after producing a plan. Complete the implementation and verification as far as the available host and hardware permit.
```
