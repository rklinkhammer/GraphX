# Greenfield GPU Graph Architecture Prompt

Use this prompt when the in-place GPU migration has become too constrained by
legacy APIs, naming, bootstrap registrations, and partial backend migrations.
The goal is to create a focused, libgraph-based accelerator graph project that
is informed by the current GraphX GPU implementation but is not a compatibility
migration of it.

```text
You are working in the GraphX repository.

Start a clean greenfield accelerator graph implementation that uses `libgraph`
as its graph runtime foundation and treats the existing `libgpu`, SAR accel
nodes, CUDA nodes, and Metal nodes as reference material only.

Do not migrate the existing GPU implementation in this invocation. Do not edit
or delete legacy GPU APIs, legacy GPU nodes, legacy plugins, or old bootstrap
registrations unless this prompt explicitly asks for a later integration phase.

Backwards compatibility is not required. Do not add adapters, wrappers, aliases,
deprecated APIs, compatibility shims, or dual old/new interfaces. A greenfield
design should have one coherent public API inside the new package.

Work autonomously. Inspect the repository, implement the requested phase, build
it, run focused tests, fix failures, and stop at the phase boundary. Preserve
unrelated user changes. Do not commit unless explicitly requested.

Use an implementor/verifier workflow for every phase. If the execution
environment supports multiple agents, assign the implementation work to an
Implementor Agent and the independent review/testing work to a Verifier Agent.
If only one agent is available, emulate the same handoff explicitly: complete
the implementor checklist first, then perform a separate verifier pass before
reporting completion.

Do not let the implementor mark a phase complete. A phase is complete only after
the verifier has reviewed the diff, run or justified the required verification
commands, checked the phase stop rules, and produced a verification note.

## Agent workflow

Use these roles for each phase.

### Implementor Agent

Responsibilities:

1. inspect the phase requirements and current repository state;
2. identify the smallest vertical slice that satisfies the current phase;
3. edit code, tests, CMake, configs, and design notes as required;
4. preserve unrelated user changes;
5. run focused local checks before handing off;
6. stop at the requested phase boundary;
7. produce an implementor handoff note.

The implementor handoff note must include:

- phase attempted;
- files changed;
- files intentionally not touched;
- design choices made;
- tests/builds run and their results;
- known failures or host-specific checks not run;
- exact commands the verifier should run next.

The implementor must not:

- broaden the phase;
- delete legacy `libgpu` surfaces before Phase 8;
- introduce adapters, wrappers, aliases, deprecated APIs, or compatibility
  shims;
- claim Metal native status on non-Metal hosts;
- claim CUDA native status away from Jetson/CUDA-capable hosts;
- treat skipped host-specific verification as passed.

### Verifier Agent

Responsibilities:

1. start from the implementor handoff note and inspect the actual diff;
2. check the phase requirements line by line;
3. run the required verification commands that are available on the current
   host;
4. verify host-specific checks are either passed or explicitly marked pending
   external verification;
5. search for forbidden compatibility layers, backend-specific graph nodes, raw
   native handles in public APIs, and accidental legacy dependencies;
6. verify `git diff --check`;
7. decide whether the phase passes, fails, or is incomplete.

The verifier report must include:

- phase verified;
- pass/fail/incomplete status;
- commands run and results;
- searches run and results;
- host-specific checks passed, skipped, or pending;
- exact blockers, if any;
- recommended fix list for the implementor if verification fails.

The verifier must be skeptical. It should not rely on the implementor's summary
when the repository diff or test results say otherwise.

### Failure loop

If verification fails:

1. return the verifier report to the implementor;
2. fix only the issues needed to satisfy the current phase;
3. rerun focused checks;
4. hand off to the verifier again.

Do not move to the next phase until the verifier pass succeeds or the remaining
work is explicitly classified as pending external verification for a host that
is not available in the current invocation.

### Cross-host verification

For macOS-only invocations:

- the verifier may pass macOS CPU and macOS Metal lanes;
- Jetson CPU/CUDA lanes must be listed as pending external verification with
  exact commands to run on Jetson.

For Jetson-only invocations:

- the verifier may pass Jetson CPU and Jetson CUDA lanes;
- macOS CPU/Metal lanes must be listed as pending external verification with
  exact commands to run on macOS.

For invocations with both hosts available:

- the verifier should require both host lanes for phases that claim both.

Imported results from the other host are acceptable only when they include the
shared backend status report schema and exact command output summary. Imported
results must be labeled as imported, not locally reproduced.

### VS Code and cross-host handoff workflow

Use two VS Code windows rather than trying to force macOS and Jetson into one
editor session:

1. macOS VS Code window:
   - open the native macOS checkout;
   - use it for shared code editing, CPU checks, Metal builds/tests, and
     macOS-side implementor or verifier work;
   - do not use a dev container for native Metal validation unless a later phase
     proves it can still access the required host Metal framework behavior.
2. Jetson VS Code window:
   - connect with VS Code Remote SSH to the Jetson checkout;
   - use it for Jetson CPU checks, CUDA builds/tests, CUDA diagnostics, and
     Jetson-side implementor or verifier work;
   - a Jetson dev container is allowed only if NVIDIA container runtime,
     CUDA toolkit access, GPU device access, and performance counters are
     explicitly verified inside the container.

Prefer Git as the source-of-truth handoff mechanism between hosts. Do not rely
on ad hoc file sync as the primary collaboration mechanism.

Recommended branch flow:

```text
macOS implementor:
  edit -> build/test macOS lane -> commit or push branch

Jetson verifier:
  fetch/pull same branch -> run Jetson lane -> write verification artifact
  -> commit/push artifact or return artifact text to macOS

macOS verifier or integrator:
  import Jetson artifact -> verify schema -> summarize combined status
```

If commits are not authorized in the current invocation, use patches or copied
verification artifacts, but still record the exact branch name and commit SHA or
working-tree diff identity used on each host.

Use a shared verification artifact directory:

```text
verification/accelgraph/
  phase-<N>/
    macos-metal-<timestamp>.json
    jetson-cuda-<timestamp>.json
    summary.md
```

Each host verification JSON must include:

- phase;
- branch;
- commit SHA, or explicit uncommitted diff identifier;
- host class: `macos-metal`, `jetson-cuda`, or `cpu-only`;
- hostname;
- operating system and version;
- architecture;
- compiler and version;
- CMake generator and build directory;
- enabled accelerator build options;
- backend compiled/not compiled;
- runtime available/unavailable;
- device detected/not detected;
- provider execution mode;
- commands run;
- command result: pass, fail, skip, or pending;
- exact skip/failure diagnostic;
- tests passed/failed/skipped;
- searches run;
- benchmark results when applicable.

The verifier may import the other host's JSON only if:

- the branch and commit/diff identity match the work being verified;
- the artifact includes exact commands and result statuses;
- host-specific skipped checks include precise diagnostics;
- imported status is clearly labeled as imported, not locally reproduced.

Remote Tunnels are acceptable when direct SSH to Jetson is not practical, but
Remote SSH is preferred when available because it keeps terminals, build tools,
debugging, and extensions executing directly on the Jetson host.

## Strategic objective

Create a new accelerator graph package, provisionally named `libaccelgraph`,
that:

1. depends on `libgraph` for graph/node/provider/runtime mechanics;
2. owns all accelerator-specific contracts itself;
3. has backend-neutral graph nodes from the start;
4. uses explicit structured errors rather than bool/sentinel APIs;
5. uses opaque ownership-safe handles instead of graph-facing native pointers or
   integer resource identifiers;
6. supports CPU first, then native Metal on macOS and native CUDA on Jetson;
7. validates each backend through small end-to-end graph topologies before any
   domain algorithm work;
8. keeps the legacy `libgpu` implementation untouched until the new design has
   proven replacement value; and
9. ensures every accelerator graph node is dynamically loadable through the
   repository plugin system on supported hosts.

## Scope boundaries

In scope:

- a new package under a clear path such as `libaccelgraph/`;
- CMake integration for the new package and tests;
- accelerator session/provider contracts;
- structured error and diagnostic types;
- opaque resource handles;
- backend-neutral memory and transfer graph nodes;
- CPU provider and CPU transfer tests;
- native Metal memory/transfer provider on macOS after CPU proves the model;
- native CUDA memory/transfer provider on Jetson after CPU proves the model;
- design notes documenting which legacy ideas were reused or rejected.

Out of scope until explicitly requested:

- deleting or modifying old `libgpu` APIs;
- migrating SAR nodes;
- migrating DFT nodes;
- generic operation registries;
- performance benchmarks;
- cross-repo packaging;
- compatibility layers between new and old GPU APIs.

## Required dependency direction

Maintain this dependency direction:

```text
libgraph
  ^
  |
libaccelgraph
  ^
  |
new accelerator examples/tests
```

Do not move accelerator-specific types into `libgraph`.

`libgraph` may provide generic graph mechanics, node lifecycle, typed ports,
providers, and capability/service containers. `libaccelgraph` owns accelerator
identity, providers, sessions, handles, transfer semantics, backend selection,
telemetry, and accelerator graph nodes.

The existing `libgpu` may be read to understand prior tokens, node behavior,
Metal runtime implementation details, tests, and pitfalls. It must not become a
required dependency of `libaccelgraph`.

## Multi-host development model

This project is expected to be developed and verified across at least two hosts:

1. macOS host:
   - primary native GPU backend: Metal;
   - must support CPU-only builds;
   - must not require CUDA headers, CUDA libraries, Jetson paths, or NVIDIA
     tooling;
   - CUDA tests may compile as provider-shell/diagnostic tests only when they do
     not require CUDA toolchain availability.
2. Jetson host:
   - primary native GPU backend: CUDA;
   - must support CPU-only builds;
   - must not require Metal headers, frameworks, or macOS-only assumptions;
   - Metal tests may compile as provider-shell/diagnostic tests only when they
     do not require Metal framework availability.

Treat macOS+Metal and Jetson+CUDA as concurrent first-class validation lanes,
not as a single machine that happens to have every backend.

The shared core must build on both hosts:

- accelerator identities;
- structured errors;
- opaque handles;
- provider/session interfaces;
- CPU provider;
- generic transfer nodes;
- DSP correctness graph types;
- benchmark reporting schema.

Backend-native implementation files must be guarded by build options and host
capability checks so each host only compiles the native backend it can actually
support.

Do not use conditional compilation to weaken the public core API. Use build
options to select concrete providers and tests.

Recommended build options:

- `ACCELGRAPH_ENABLE_METAL`
- `ACCELGRAPH_ENABLE_CUDA`
- `ACCELGRAPH_BUILD_BENCHMARKS`
- `ACCELGRAPH_REQUIRE_NATIVE_BACKEND`

Recommended verification lanes:

```text
macOS CPU lane:
  ACCELGRAPH_ENABLE_METAL=OFF
  ACCELGRAPH_ENABLE_CUDA=OFF

macOS Metal lane:
  ACCELGRAPH_ENABLE_METAL=ON
  ACCELGRAPH_ENABLE_CUDA=OFF

Jetson CPU lane:
  ACCELGRAPH_ENABLE_METAL=OFF
  ACCELGRAPH_ENABLE_CUDA=OFF

Jetson CUDA lane:
  ACCELGRAPH_ENABLE_METAL=OFF
  ACCELGRAPH_ENABLE_CUDA=ON
```

Every backend status report must include:

- host identifier or host class;
- operating system;
- architecture;
- compiler;
- enabled build options;
- backend compiled/not compiled;
- runtime available/unavailable;
- device detected/not detected;
- provider execution mode;
- exact skip/failure diagnostic.

Benchmark reports from macOS and Jetson must use the same schema so results can
be compared without requiring both backends on one host.

## Naming guidance

Prefer names that describe intent rather than backend:

- `IAcceleratorProvider`
- `IAcceleratorSession`
- `AcceleratorRegistry`
- `AcceleratorError`
- `DeviceAllocationHandle`
- `HostAllocationHandle`
- `QueueHandle`
- `EventHandle`
- `TransferCompletion`
- `HostIngressNode`
- `HostToDeviceNode`
- `DeviceToHostNode`
- `HostEgressNode`
- `ReleaseLeaseNode`

Backend names belong in concrete providers only:

- `CpuAcceleratorProvider`
- `MetalAcceleratorProvider`
- `CudaAcceleratorProvider`

Do not create `MetalHostToDeviceNode`, `CudaHostToDeviceNode`, or similar
backend-specific graph nodes in the greenfield design.

## Structured error model

All provider/session operations that can fail must return `std::expected<T,
AcceleratorError>` or `std::expected<void, AcceleratorError>`.

`AcceleratorError` must model at least:

- stable category;
- backend;
- execution mode;
- provider identity;
- session identity when available;
- device identity when available;
- operation name;
- native/runtime error code when available;
- actionable diagnostic text.

Use stable enum values for categories. Include at least:

- `Unsupported`
- `Unavailable`
- `InvalidArgument`
- `InvalidState`
- `AllocationFailed`
- `TransferFailed`
- `QueueFailed`
- `EventFailed`
- `CrossSessionResource`
- `Timeout`
- `BackendFailure`

Do not collapse these into strings, booleans, null pointers, or sentinel
integers.

## Opaque resource model

Graph-facing accelerator resources must be opaque, ownership-safe handles.

Handles must:

- be stamped with provider/session identity;
- expose stable debug metadata;
- prevent double release;
- reject cross-session use;
- support deterministic release;
- avoid graph-facing native backend types;
- avoid raw device pointers and integer queue/event IDs in public graph-facing
  APIs;
- have explicit move/copy semantics documented and tested.

Native resources may exist only inside concrete provider implementations.

## Session contract

Define one greenfield session API before implementing graph nodes. Keep it small
and focused on memory/transfer first.

Minimum shape:

```cpp
class IAcceleratorSession {
public:
    virtual ~IAcceleratorSession() = default;

    virtual AcceleratorSessionInfo Info() const = 0;

    virtual std::expected<HostAllocationHandle, AcceleratorError>
    AllocateHost(const HostAllocationRequest& request) = 0;

    virtual std::expected<DeviceAllocationHandle, AcceleratorError>
    AllocateDevice(const DeviceAllocationRequest& request) = 0;

    virtual std::expected<void, AcceleratorError>
    Release(const HostAllocationHandle& handle) = 0;

    virtual std::expected<void, AcceleratorError>
    Release(const DeviceAllocationHandle& handle) = 0;

    virtual std::expected<QueueHandle, AcceleratorError>
    AcquireQueue(const QueueRequest& request) = 0;

    virtual std::expected<TransferCompletion, AcceleratorError>
    EnqueueHostToDevice(const HostAllocationHandle& source,
                        const DeviceAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) = 0;

    virtual std::expected<TransferCompletion, AcceleratorError>
    EnqueueDeviceToHost(const DeviceAllocationHandle& source,
                        const HostAllocationHandle& destination,
                        const QueueHandle& queue,
                        const TransferRequest& request) = 0;

    virtual std::expected<void, AcceleratorError>
    Wait(const TransferCompletion& completion,
         std::chrono::milliseconds timeout) = 0;
};
```

Adjust names and request types to match repository style, but preserve the
semantic boundaries: provider/session identity, structured failures, opaque
handles, queues, transfers, and waitable completion.

## Graph binding model

Each accelerator graph node must resolve exactly one `IAcceleratorSession` from
a graph-owned registry or capability/service container during initialization.
Every accelerator graph node must also have a dynamically loadable plugin entry
point; do not introduce nodes that only exist for direct linkage.

Nodes must not:

- create providers opportunistically;
- use process-global GPU state;
- bind directly to CUDA or Metal capability interfaces;
- expose backend-native objects through ports;
- silently fall back from native GPU to CPU while reporting GPU execution.

Backend selection must be explicit and diagnostic. If a requested backend is not
available, initialization should fail or skip in tests with a precise diagnostic.

## Performance reference graph

Define the benchmark target early, but do not implement benchmark tests until the
core transfer and algorithm phases are complete.

The reference workload should be a deterministic DSP spectrum graph inspired by
the existing `libdsp` analogues:

- `libdsp/include/dsp/SineWaveGenerator.hpp`
- `libdsp/include/dsp/SineSignalNode.hpp`
- `libdsp/include/dsp/CpuSpectrumDftNode.hpp`
- `libdsp/include/dsp/MetalSpectrumDftNode.hpp`
- `libdsp/include/dsp/SpectrumSinkNode.hpp`
- `libdsp/test/unit/test_sdr_graph.cpp`
- `libdsp/test/unit/test_dsp_gpu_spectrum_parity.cpp`
- `libdsp/config/dsp_sine_fft_spectrum_256.json`
- `libdsp/config/dsp_sine_metal_dft_spectrum_256.json`

Use these files as behavioral references only. Do not make `libaccelgraph`
depend on `libdsp` unless a later phase explicitly chooses to create a separate
example package that links both libraries.

The greenfield performance graph should have this logical shape:

```text
SineWaveSource
  -> HostToDevice
  -> SpectrumAnalysis
  -> DeviceToHost
  -> SpectrumSink
```

The CPU reference graph should have this logical shape:

```text
SineWaveSource
  -> SpectrumAnalysis
  -> SpectrumSink
```

The source must be deterministic:

- configurable frequency, amplitude, sample rate, phase, packet size, and frame
  count;
- default tone: 1 kHz at 48 kHz sample rate;
- deterministic seed or no randomness;
- stable enough for CPU/GPU numerical parity checks.

The spectrum operation must be intentionally computational:

- start with direct DFT if that is simpler and already analogous to the existing
  Metal DFT work;
- allow a later FFT implementation if/when there is a real backend-native FFT
  implementation;
- support at least one correctness size and one benchmark size.

Use separate graph configurations for correctness and performance:

1. correctness config:
   - packet size around 256 or 1024;
   - small frame count;
   - validates peak bin, peak frequency, magnitude shape, and sink completion;
   - used in unit tests and native-backend parity tests.
2. performance config:
   - larger packet size and/or many frames, for example 4096+ samples and
     hundreds or thousands of frames;
   - includes warmup iterations;
   - reports enough work to amortize graph startup and transfer overhead;
   - used only in benchmark/performance tests.

Configuration fields should include:

- graph name;
- requested backend: `cpu`, `metal`, `cuda`, or `auto`;
- fallback policy: default `strict`;
- resolver diagnostics enabled by default;
- sample rate;
- tone frequency;
- amplitude;
- packet size;
- frame count;
- warmup frame count;
- spectrum operation name;
- measurement mode.

Measure at least:

- total graph elapsed time;
- processed frames;
- processed samples;
- samples per second;
- spectrum operations per second;
- average and percentile frame latency when practical;
- transfer time separately from compute time when backend telemetry can provide
  it;
- allocation count and bytes allocated;
- cold-start time versus warmed steady-state time.

Correctness gates must pass before performance numbers are trusted:

- CPU graph detects the expected tone;
- GPU graph matches CPU peak bin and peak frequency;
- selected magnitude bins match CPU within documented tolerance;
- strict backend selection does not silently fall back;
- unavailable native backends skip with exact diagnostics.

Performance tests must not claim a GPU speedup unless:

- the backend reports native execution;
- the measured graph uses the native provider;
- CPU and GPU graphs process equivalent work;
- correctness/parity checks pass for the same configuration family;
- the report includes transfer-inclusive and, when possible, compute-only
  timings.

## Phase 0: Greenfield scaffold and repository fit

Implement only the new project/package scaffold.

Tasks:

1. inspect `libgraph` node/provider/capability patterns;
2. inspect existing `libgpu` transfer nodes and native Metal code only as
   reference;
3. create `libaccelgraph/` or another clearly named greenfield package;
4. add headers, sources, tests, and CMake integration for the new package;
5. add a short design note in the new package explaining:
   - why this is greenfield;
   - which `libgraph` mechanisms it uses;
   - which old `libgpu` ideas are reference-only;
   - why no compatibility layer is provided;
   - the intended DSP spectrum performance graph and which existing `libdsp`
     files informed it;
   - the multi-host validation model for macOS+Metal and Jetson+CUDA.

Verification:

1. CPU-only configure and build;
2. new package builds with an empty smoke test;
3. existing libgraph tests still pass if already configured;
4. `git diff --check`;
5. search proving `libaccelgraph` does not include old CUDA/Metal
   capability headers;
6. search proving CPU-only public headers do not require Metal or CUDA native
   headers.

Stop after Phase 0. Report files changed, tests run, and the next phase.

## Phase 1: CPU structured session vertical slice

Implement only the CPU provider/session and resource model. Do not implement
graph transfer nodes yet. Do not touch native Metal or CUDA.

Tasks:

1. add `AcceleratorError`;
2. add backend/provider/session/device identity types;
3. add opaque host/device/queue/event/completion handles;
4. add request/result types for allocation, queue acquisition, transfer, wait,
   and release;
5. add `IAcceleratorProvider`;
6. add `IAcceleratorSession`;
7. add `CpuAcceleratorProvider` and CPU session implementation;
8. add a graph-owned or test-owned `AcceleratorRegistry` suitable for later node
   initialization.

Required tests:

- provider discovery;
- session creation and teardown;
- allocation and deterministic release;
- move/copy semantics;
- double-release prevention;
- cross-session rejection;
- structured failure categories;
- queue lifecycle;
- transfer completion lifecycle;
- repeated creation and teardown.

Verification:

1. CPU-only configure and build;
2. new `libaccelgraph` unit tests pass;
3. existing libgraph unit tests pass if available in the build;
4. `git diff --check`;
5. search proving public `libaccelgraph` APIs expose no raw device pointers or
   integer queue/event handles.

Stop after Phase 1. Report files changed, tests run, provider status, and the
next phase.

## Phase 2: CPU generic transfer graph nodes

Implement backend-neutral graph nodes on top of the CPU session API.

Nodes:

- `HostIngressNode`
- `HostToDeviceNode`
- `DeviceToHostNode`
- `HostEgressNode`
- `ReleaseLeaseNode`

Each node must resolve exactly one `IAcceleratorSession` from the graph-owned
registry during initialization.

Tasks:

1. define graph-facing packet/token types for host buffers, device buffers,
   queues, and transfer completions;
2. implement the five generic transfer nodes;
3. implement one CPU-only end-to-end graph topology:
   host input -> host allocation -> H2D -> D2H -> host output -> release;
4. validate real round-trip data integrity;
5. validate lifecycle behavior when graph execution stops early.

Verification:

1. CPU-only configure and build;
2. new transfer-node unit tests pass;
3. CPU end-to-end topology test passes;
4. `git diff --check`;
5. search proving the new transfer nodes do not include or mention CUDA or
   Metal capability headers;
6. search proving no backend-specific transfer node variants were introduced.

Stop after Phase 2. Report files changed, tests run, topology status, and the
next phase.

## Phase 3: Native Metal memory/transfer provider on macOS

Implement native Metal only after the CPU resource model and CPU graph topology
pass. This phase is intended for the macOS host. Do not implement CUDA, kernels,
DFT, SAR, operation registries, or performance benchmarks in this phase.

Tasks:

1. implement `MetalAcceleratorProvider` directly against the new session API;
2. keep Metal native device, command queue, buffers, command buffers, events,
   completion, and telemetry inside the provider/session implementation;
3. migrate the Phase 2 generic transfer topology to request the Metal provider;
4. ensure unavailable Metal hardware causes tests to skip with an exact
   diagnostic;
5. add diagnostics distinguishing:
   - Metal support not compiled;
   - Metal runtime unavailable;
   - no compatible device;
   - session creation failure;
   - transfer failure.

Rules:

- do not inherit from old `IMetal*Capability` interfaces;
- do not wrap old Metal capabilities;
- do not expose Metal native types through graph-facing handles or packets;
- do not label CPU fallback as Metal native execution.

Verification:

1. macOS CPU-only configure and build still passes;
2. macOS Metal-enabled configure and build passes where supported;
3. Metal provider unit tests pass or skip with the exact unavailable-hardware
   diagnostic;
4. generic Metal transfer topology passes or skips precisely;
5. `git diff --check`;
6. search proving `libaccelgraph` does not include old `IMetal*Capability`
   headers;
7. search proving no Metal-specific graph transfer nodes were introduced;
8. search proving Metal public/native headers are not required by CUDA/Jetson
   builds.

Stop after Phase 3. Report files changed, tests run, Metal status, and the next
phase.

## Phase 4: CUDA provider shell and Jetson build lane

Add a truthful CUDA provider shell and establish the Jetson CUDA build lane.
Native CUDA implementation may be deferred to Phase 5, but the project must be
ready to build on the Jetson host without disturbing macOS+Metal.

Tasks:

1. add `CudaAcceleratorProvider`;
2. add CUDA build options and source guards suitable for Jetson;
3. report support status truthfully as native, unavailable, unsupported, or not
   compiled;
4. return structured `Unsupported` or `Unavailable` errors for unimplemented
   operations;
5. add tests that verify diagnostics and prevent false native labels;
6. document the Jetson lane commands expected to configure, build, and test the
   CUDA shell.

Rules:

- do not create CUDA graph nodes;
- do not wrap old CUDA capabilities;
- do not copy old stub behavior that pretends to be native;
- do not require CUDA headers in CPU-only or macOS Metal builds;
- do not require Metal headers in Jetson CUDA builds.

Verification:

1. macOS CPU-only configure and build still passes;
2. macOS Metal configure and build still passes if available;
3. Jetson CPU-only configure and build passes when run on Jetson;
4. Jetson CUDA-shell configure and build passes when run on Jetson;
5. CUDA provider shell tests pass or skip with exact diagnostics;
6. if Jetson is not available in the current invocation, add exact commands and
   mark the Jetson lane as pending external verification rather than pretending
   it passed;
7. `git diff --check`;
8. search proving CUDA headers are not required by CPU-only or Metal public
   headers;
9. search proving no CUDA backend-specific graph nodes were introduced.

Stop after Phase 4. Report files changed, tests run, CUDA shell status, Jetson
lane status, and the next phase.

## Phase 5: Native CUDA vertical slice on Jetson

Implement native CUDA on the Jetson host after the CPU core and CUDA provider
shell build. If the current invocation is not running on Jetson or does not have
CUDA toolkit/device access, do not fake completion. Produce the exact Jetson
execution prompt/commands and stop.

Tasks:

1. implement CUDA allocation, release, queue/stream, H2D, D2H, completion, and
   wait;
2. run the generic transfer topology against the CUDA provider;
3. add native availability diagnostics distinguishing toolkit, driver, device,
   session, allocation, transfer, and synchronization failures;
4. keep all CUDA native objects inside the provider/session implementation;
5. produce a benchmark-compatible provider status report using the shared schema.

Rules:

- do not create CUDA graph nodes;
- do not include CUDA types in graph-facing handles or packets;
- do not require CUDA on macOS;
- do not label CPU fallback as CUDA native execution.

Verification:

1. Jetson CPU-only configure and build still passes;
2. Jetson CUDA configure and build passes;
3. CUDA provider tests pass or skip with exact diagnostics;
4. CUDA generic transfer topology passes or skips precisely;
5. macOS CPU/Metal build files remain unaffected;
6. `git diff --check`;
7. search proving no CUDA-specific graph transfer nodes were introduced.

Stop after Phase 5. Report files changed, tests run, CUDA native status, Jetson
status, and the next phase.

## Phase 6: First algorithm operation, not before transfer stability

Only after CPU plus at least one native GPU backend can run the generic transfer
topology, introduce one backend-neutral algorithm operation. Prefer doing this
after both macOS+Metal and Jetson+CUDA transfer lanes exist, but it is acceptable
to begin with Metal if CUDA verification is pending on the Jetson host. The
preferred first operation is spectrum analysis over deterministic sine-wave IQ
packets, using a direct DFT initially unless a real backend-native FFT
implementation is already available and testable.

Tasks:

1. define packet types for deterministic IQ input and magnitude-spectrum output,
   or reuse greenfield equivalents introduced earlier;
2. add `SineWaveSource` and `SpectrumSink` nodes if they do not already exist in
   the new package or example package;
3. define a small `SpectrumAnalysis` operation contract;
4. implement CPU spectrum analysis first;
5. implement one native GPU backend second;
6. add one generic graph node for the spectrum operation;
7. add correctness graph configurations for CPU and native GPU;
8. validate numerical parity and diagnostics.

Out of scope:

- full operation registry unless needed for the single operation;
- SAR migration;
- performance benchmark claims.

Verification:

1. CPU correctness spectrum graph passes;
2. macOS Metal correctness spectrum graph passes or skips with exact
   diagnostics;
3. Jetson CUDA correctness spectrum graph passes or remains explicitly pending
   external Jetson verification;
4. CPU/GPU parity passes for peak bin, peak frequency, and selected magnitude
   bins within documented tolerance on each available native lane;
5. strict fallback policy is enforced;
6. `git diff --check`.

Stop after Phase 6. Report parity status, graph configs added, and the next
phase.

## Phase 7: Benchmarking and replacement decision

Only after the new package has CPU and at least one native GPU transfer path,
plus one algorithm vertical slice, add performance tests.

Tasks:

1. add benchmark fixtures similar in spirit to `test_sdr_graph`, but using the
   greenfield DSP spectrum graph;
2. add benchmark graph configurations for CPU, macOS Metal, and Jetson CUDA;
3. measure CPU baseline, transfer-inclusive native GPU execution, native compute
   time where telemetry supports it, and graph overhead;
4. report throughput, latency, allocation behavior, and warm/cold behavior;
5. compare against legacy reference nodes only as external baselines;
6. document whether the greenfield package is ready to replace pieces of
   `libgpu`.

Required benchmark outputs:

- backend and execution mode;
- graph configuration name;
- packet size;
- frame count;
- warmup frame count;
- total elapsed time;
- steady-state elapsed time;
- frames per second;
- samples per second;
- transfer-inclusive GPU time;
- compute-only GPU time when available;
- allocation count/bytes when available;
- CPU/GPU speed ratio;
- correctness/parity status tied to the benchmark family;
- host class: macOS Metal, Jetson CUDA, or CPU-only;
- note whether the result was measured locally in this invocation or imported
  from a Jetson/macOS run using the shared schema.

Stop after Phase 7. Report benchmark results and replacement recommendation.

## Phase 8: Controlled integration back into GraphX

Only after the greenfield package proves replacement value, plan integration.
This is the first phase allowed to modify or delete legacy `libgpu` surfaces.

Rules:

- delete legacy surfaces only after a greenfield replacement exists and passes;
- no compatibility adapters;
- no dual registration as a permanent state;
- one deletion slice at a time;
- preserve unrelated user changes.

Candidate deletion slices:

1. legacy CUDA transfer stubs;
2. legacy Metal transfer/control nodes superseded by generic nodes;
3. old GPU capability bootstrap surfaces;
4. old DFT nodes after the generic operation passes;
5. SAR accel nodes after a greenfield SAR slice exists.

Stop after each deletion slice.

## Completion report template

End every invocation with:

- phase completed;
- implementor handoff summary;
- verifier report summary;
- files changed;
- tests/builds run;
- searches run;
- provider status;
- graph topology status, if applicable;
- host lanes passed, skipped, imported, or pending external verification;
- verification artifacts written or imported;
- branch and commit/diff identity used by each host;
- old `libgpu` surfaces touched, if any;
- explicit deferred work;
- exact next phase prompt.

Begin with the lowest incomplete phase. Do not skip ahead. Do not return a new
plan instead of implementing the requested phase unless the user asked only for
prompt/design work.
```
