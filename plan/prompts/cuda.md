# CPU, CUDA, and Metal Node Parity Implementation Prompt

```text
You are working in the GraphX repository. Establish a truthful, tested CPU/CUDA/Metal backend comparison by completing one end-to-end DSP vertical slice first, then expanding backend-node parity only after that slice passes its acceptance gates.

Work autonomously: inspect the repository, implement the changes, build them, run relevant tests, and fix failures. Preserve unrelated user changes. Follow the repository's existing toolchain and CMake conventions; do not hardcode host compilers or bypass configured presets.

Do not attempt a repository-wide backend redesign in one pass. Work in the phases below, and keep every phase independently buildable and reviewable.

## Phase 0: Audit and design record

Audit the existing Metal nodes in:

- `libgpu/include/gpu/metal/nodes`
- `libgpu/src/gpu/metal/nodes`
- `libgpu/plugins`
- Metal domain nodes in `libdsp` and `examples`

Also inspect the existing CUDA capability and node surfaces before adding new abstractions.

Create or update a checked-in parity matrix containing:

- Metal node and classification: compute, transfer, memory, synchronization, or control;
- corresponding logical operation, rather than merely a matching class name;
- existing CPU and CUDA implementation, if any;
- native, stub, simulated, fallback, or unavailable execution status;
- proposed CPU and CUDA node names where implementations are meaningful;
- shared graph-facing contract and backend-specific semantic differences;
- rationale for any intentionally backend-specific or non-applicable operation.

The matrix must allow `implemented`, `not applicable`, and `deferred` outcomes. Do not invent CPU nodes for GPU resource-lifecycle operations such as peer-device copies, device allocation, or queue synchronization merely to make the table look symmetrical.

Before implementation, record the proposed reusable contracts, build targets, tests, and benchmark executable. Then proceed directly to Phase 1; do not stop after producing the audit.

## Phase 1: Native CUDA foundation and DSP vertical slice

Use the existing CPU and Metal spectrum DFT path as the first complete vertical slice:

- `CpuSpectrumDftNode`
- `MetalSpectrumDftNode`
- the source -> DFT -> spectrum-sink topology demonstrated by `libdsp/test/unit/test_sdr_graph.cpp`

Implement a native `CudaSpectrumDftNode` with the same logical input/output contract and numerically equivalent direct-DFT algorithm. Preserve truthful DFT naming; do not call it an FFT unless an actual FFT algorithm or library is used.

### CUDA build and runtime requirements

- Detect the CUDA compiler and toolkit through CMake without breaking CPU-only or Metal-only hosts.
- Compile native kernels as `.cu` sources. Define and document the separable-compilation/device-link policy.
- Add an explicit minimum supported CUDA toolkit version based on the APIs actually used.
- Make CUDA architectures configurable through standard CMake mechanisms. Do not hardcode a desktop architecture; support Jetson builds and document the selected architecture in diagnostics.
- Clearly separate native CUDA capability from the existing stub capability. A configured stub must never satisfy a native-CUDA availability check.
- Report toolkit/runtime version, device name, compute capability, selected device, and native availability where diagnostics are emitted.
- Handle no-driver, no-device, unsupported-device, allocation, copy, launch, and synchronization failures with actionable errors.
- Implement real CUDA allocation, transfers, streams/events, kernel launch, synchronization, and error propagation where the vertical slice requires them.
- CPU fallback may be offered only when explicitly configured and must report itself as CPU fallback. It must never be benchmarked or labeled as native CUDA.
- Preserve existing CPU and Metal behavior and keep CPU-only builds functional.

### Graph-facing parity

Align the logical contracts across CPU, CUDA, and Metal:

- input/output token meaning and ownership;
- port indices;
- sample count and scalar type;
- configuration fields that are inherently backend-neutral;
- lifecycle behavior;
- plugin registration, discovery, and dynamic construction;
- result metadata and error reporting.

Do not force backend implementation details to be identical. Streams and events, Metal command queues and buffers, and host execution have different resource and synchronization semantics. Document those differences while preserving equivalent externally observable graph behavior.

### Phase 1 correctness tests

Add tests proving:

- CPU, CUDA, and Metal DFT outputs agree within justified absolute and relative tolerances;
- known-tone peak-bin and magnitude behavior is correct;
- dynamic plugin creation succeeds for each available backend;
- graph topology, ports, token meaning, and result metadata agree;
- invalid configuration and runtime failures are reported consistently;
- native CUDA execution is observable through diagnostics and cannot be confused with a stub or CPU fallback;
- CUDA hardware tests skip with a precise reason when native CUDA or a compatible device is unavailable;
- existing tests continue to pass.

## Phase 2: Reproducible SDR graph performance suite

Create a dedicated performance executable or test target inspired by the graph construction and plugin-loading approach in `libdsp/test/unit/test_sdr_graph.cpp`. That existing file is a correctness test, not a benchmark: do not add fragile speed assertions to it or to ordinary unit tests.

Build equivalent CPU, native CUDA, and native Metal source -> direct DFT -> sink graphs using identical deterministic input samples and output validation.

### Required workload matrix

Use direct-DFT packet sizes:

- small: 64 samples;
- baseline: 256 samples;
- medium: 1024 samples;
- large: 4096 samples.

Because direct DFT is O(N^2), permit the benchmark to omit or reduce iterations for a size that exceeds a documented runtime limit. Report the omission explicitly; do not silently substitute an FFT or a different workload.

Defaults:

- 10 untimed warm-up iterations per backend and workload;
- at least 30 measured iterations for 64, 256, and 1024 samples;
- at least 10 measured iterations for 4096 samples;
- configurable warm-up count, measured count, and maximum runtime;
- deterministic inputs generated once and reused across backends;
- output validation before a timing sample is accepted.

### Measurement scopes

Measure and label these scopes separately:

1. initialization/cold start: plugin loading, graph creation, backend/context initialization, allocation, and first execution;
2. compute-only: the DFT operation with required data already resident in the appropriate memory;
3. transfer-only: H2D and D2H separately where applicable;
4. transfer-inclusive operation: H2D + compute + D2H;
5. warmed end-to-end graph latency;
6. steady-state graph throughput in frames/second and samples/second.

Use CUDA events for CUDA device-operation timings. Use the corresponding Metal device timing mechanism when reliable and available. Use a monotonic host clock for host and end-to-end measurements. Synchronize device work before stopping any host timer that includes GPU work. Never compare an asynchronous enqueue duration with synchronous CPU execution.

Use Release builds. Disable or separately account for debug logging and validation overhead. Warm each backend before steady-state measurement. Keep allocation and context initialization outside steady-state scopes but report them separately.

Run measurements in a stable order or rotate backend order to reduce thermal/order bias. Record caveats for GPU clock scaling, power mode, shared-memory pressure, and other uncontrolled host conditions. Do not claim results are portable across machines.

### Reporting

For every backend, workload, and measurement scope, report:

- availability and native/stub/fallback status;
- accepted sample count;
- median, arithmetic mean, standard deviation, p95, minimum, and maximum;
- device and runtime metadata;
- build type and relevant benchmark configuration;
- validation result;
- unavailable or omitted reason where applicable.

Emit concise human-readable output plus machine-readable JSON or CSV with a documented schema.

Calculate speedup as `CPU duration / GPU duration`. Clearly distinguish compute-only, transfer-inclusive, and end-to-end speedups. Report whether a CPU/GPU crossover exists within the tested range; do not assume that one exists and do not claim a performance gain unless measurements demonstrate it.

Performance results are informational by default and must not fail ordinary CI merely because a backend is slower on a particular machine. Add a regression threshold only if a stable baseline, controlled runner, metric scope, and documented tolerance exist.

## Phase 3: Expand node parity deliberately

Proceed to broader node implementation only after Phases 1 and 2 build and their correctness tests pass.

Use the parity matrix to prioritize additional nodes:

1. reusable native CUDA transfer and memory primitives required by real compute graphs;
2. generic compute nodes with a defined operation and test oracle;
3. domain algorithms that have CPU reference implementations and representative fixtures;
4. backend-specific control or collective operations only when a real use case exists.

For each added node:

- define the logical cross-backend contract;
- document backend-specific ownership and synchronization semantics;
- implement real host work for CPU algorithm nodes;
- implement real kernels or native runtime operations for CUDA nodes;
- add plugin/configuration/CMake/install integration consistent with repository conventions;
- add numerical or behavioral parity tests;
- update the parity matrix;
- do not label transfer, allocation, synchronization, or token forwarding as algorithm acceleration.

Do not require a CPU analogue when the parity matrix marks the operation `not applicable`. Do not implement all deferred Metal counterparts merely to satisfy nominal class-name symmetry.

## Acceptance gates

### Gate A: vertical slice

- CPU-only configuration builds and tests successfully.
- CUDA-enabled configuration detects native CUDA independently from stubs.
- `CudaSpectrumDftNode` executes a real CUDA kernel on supported hardware.
- CPU/CUDA/Metal numerical parity tests pass on available backends.
- Dynamic plugin and end-to-end graph tests pass.

### Gate B: performance suite

- Equivalent direct-DFT work is compared across backends.
- Timing scopes are not conflated.
- Reports are reproducible, machine-readable, and truthful about unavailable hardware.
- Results report observed crossover behavior without presupposing acceleration.

### Gate C: broader parity

- Every audited Metal node is marked implemented, not applicable, or deferred with rationale.
- Every newly implemented node has correctness coverage and truthful native-execution diagnostics.
- Existing GraphX test lanes do not regress.

## Final report

Finish with:

- files changed;
- updated parity matrix;
- architecture and contract decisions;
- build and test commands executed;
- test results and skipped-hardware reasons;
- benchmark methodology and configuration;
- measured results when supported hardware is available;
- observed crossover behavior, if any;
- deferred nodes and remaining limitations.

Complete Phases 0-2. Treat Phase 3 as a prioritized follow-on: implement only the additional nodes that can be completed and verified without weakening Gates A or B. Do not stop after producing a plan, and do not claim native CUDA success without executing on supported CUDA hardware.
```
