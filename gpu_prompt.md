# Generic GPU Node Architecture Implementation Prompt

```text
You are working in the GraphX repository. Replace the existing GPU node architecture with a backend-neutral accelerator session and operation model that supports truthful native CUDA, Metal, and SYCL execution.

Use `gpu_architecture.md` as the architectural rationale and starting analysis. Verify its observations against the current code before changing anything.

Work autonomously: inspect the repository, implement the requested phases, build them, run relevant tests, and fix failures. Preserve unrelated user changes. Keep each phase independently buildable and reviewable, but treat this as a clean-break replacement rather than a backwards-compatible migration.

## Clean-break mandate

Backwards compatibility is explicitly not required for the GPU architecture.

- Remove obsolete GPU capability interfaces, backend-specific infrastructure nodes, plugin types, configuration forms, serialized token fields, bootstrap paths, and tests when the replacement is complete.
- Do not add compatibility wrappers, aliases, shims, translation layers, dual registration, deprecated fields, or adapter interfaces whose purpose is to preserve an old API.
- Do not keep old and new GPU architectures alive in parallel beyond the shortest implementation interval needed to keep an individual change buildable.
- Update all in-repository callers, plugins, configurations, examples, documentation, and tests to the new API.
- Breaking changes to GPU node names, plugin identifiers, ports, configuration, topology JSON, token layouts, and plugin ABI are allowed and expected when they produce a cleaner architecture.
- Delete superseded APIs after their callers have been migrated. Do not leave a deprecated compatibility surface for hypothetical external users.
- Preserve unrelated non-GPU public APIs and behavior unless a change is technically necessary and documented.

## Desired outcome

GraphX should support:

- backend-neutral graph nodes for common GPU operations;
- coherent graph-scoped accelerator sessions;
- backend-neutral memory, transfer, event, execution, telemetry, and operation capabilities;
- native CUDA, Metal, and SYCL providers behind those interfaces;
- explicit native, stub, simulated, fallback, and unavailable execution modes;
- domain algorithm resolution by operation intent;
- backend-specific extension capabilities for features that are not honestly portable; and
- one authoritative GPU architecture with no retained legacy API surface.

Do not create one monolithic universal `GpuNode`. Separate graph-node intent, accelerator resource management, domain operations, and backend-native implementation.

## Architectural principles

Reuse these concepts where they remain the cleanest solution, but redesign or replace their APIs when needed:

- `CapabilityBus` as the graph-owned dependency-injection mechanism;
- `IGpuCapabilityBinding` as the GPU-node initialization hook;
- `GpuPolicy` binding during graph initialization;
- plugin integration for capability-bound nodes;
- `ResolvingNodeProvider` for graph-level implementation selection;
- backend-neutral accelerator tokens and domain sidecars; and
- truthful distinction between compute, transfer, memory, synchronization, and control operations.

Document all breaking GPU changes and update every in-repository use. Do not add a compatibility path.

## Phase 0: Audit and design record

Inspect at least:

- `libgraph/include/graph/CapabilityBus.hpp`
- `libgraph/include/graph/IGpuCapabilityBinding.hpp`
- `libgraph/include/policies/GpuPolicy.hpp`
- `libgraph/src/graph/ResolvingNodeProvider.cpp`
- `libgpu/include/gpu/accel/types`
- `libgpu/include/gpu/{cuda,metal,sycl}/capabilities`
- `libgpu/include/gpu/{cuda,metal,sycl}/nodes`
- `libgpu/src/gpu/GpuCapabilityBootstrap.cpp`
- native Metal runtime implementations
- CUDA and SYCL default/stub implementations
- DSP GPU transfer and spectrum nodes
- SAR `Accel` nodes and Metal domain nodes
- GPU plugin registrations and tests

Produce a checked-in design record containing:

- current capability and node inventory;
- native versus stub/simulated status for each backend;
- duplicated logical contracts across CUDA, Metal, and SYCL;
- proposed generic interfaces and their ownership boundaries;
- backend-specific semantic differences that must remain extensions;
- plugin and configuration replacement strategy;
- dependency direction between `libgraph`, `libgpu`, `libdsp`, and examples;
- replacement sequence and deletion points; and
- risks and rollback points.

Do not move GPU-specific types into `libgraph`. `libgraph` may own generic service-container and binding mechanics, while accelerator contracts belong in `libgpu`.

Proceed to Phase 1 after recording the design. Do not stop after producing a plan.

## Phase 1: Execution identity and backend descriptors

Add backend-neutral descriptors in `libgpu` for runtime identity and truth-in-labeling.

At minimum model:

```cpp
enum class ExecutionMode {
    Native,
    Stub,
    Simulated,
    CpuFallback,
    Unavailable,
};

struct BackendDescriptor {
    accel::BackendKind backend;
    ExecutionMode execution_mode;
    std::string provider_name;
    std::string runtime_version;
    std::string device_name;
    std::uint32_t device_id;
    std::string architecture;
    std::uint64_t session_id;
    FeatureSet features;
};
```

Exact names may be adjusted to repository conventions, but the model must explicitly distinguish native execution from stubs, simulation, and fallback.

Requirements:

- Every registered GPU provider can describe itself.
- Diagnostics expose backend, execution mode, provider, device, runtime, and session identity.
- A stub or simulated implementation must never satisfy a request requiring native execution.
- Update all affected enum users, serializers, parsers, formatters, configurations, and tests together; numeric compatibility is not required.
- CPU execution is represented deliberately; do not mislabel ordinary host memory as CUDA, Metal, or SYCL.

Add unit tests for descriptor validation, execution-mode filtering, formatting, and serialization where applicable.

## Phase 2: Coherent accelerator sessions

Introduce a backend-neutral `IAcceleratorSession` in `libgpu`.

A session represents one coherent backend runtime, selected device, context, queue/event domain, and related services. It must prevent a node from accidentally combining memory from one runtime context with queues or events from another.

Prefer narrow sub-capabilities exposed by the session rather than a god interface:

```cpp
class IAcceleratorSession {
public:
    virtual ~IAcceleratorSession() = default;
    virtual const BackendDescriptor& Describe() const = 0;
    virtual IMemoryCapability& Memory() = 0;
    virtual ITransferCapability& Transfer() = 0;
    virtual IEventCapability& Events() = 0;
    virtual IExecutionCapability& Execution() = 0;
    virtual ITelemetryCapability& Telemetry() = 0;
};
```

Use repository-appropriate result and ownership types. Avoid raw owning pointers.

Define backend-neutral common capabilities for operations whose semantics genuinely align:

- device and host-visible allocation;
- lease release;
- H2D, D2H, and D2D transfer;
- queue or stream acquisition;
- completion event creation, query, and wait;
- kernel or operation submission;
- telemetry and structured error reporting.

Do not force peer copy, collectives, shared/unified memory, runtime compilation, or backend-specific kernel descriptors into a lowest-common-denominator interface. Expose these through optional extension capabilities or feature queries.

### Session registration and resolution

Extend the capability mechanism to resolve more than one provider. A request should be able to express:

- required capability or session type;
- preferred or required backend;
- required execution mode;
- device preference;
- required feature set; and
- fallback policy.

Return a structured resolution containing the selected session, descriptor, and diagnostics. Do not rely only on `bool` or a nullable pointer.

Replace typed-only GPU capability lookup with the richer resolution mechanism where required. Remove superseded GPU registrations and interfaces rather than retaining parallel lookup paths.

The graph-owned capability bus is authoritative during execution. Remove the process-global shared GPU bus and its override machinery if no non-GPU subsystem genuinely requires them after migration.

## Phase 3: Backend providers

Implement session providers for the currently available backends directly against the new interfaces. Do not implement providers by preserving the old backend capability APIs behind adapter interfaces.

### Metal

Refactor the useful native Metal runtime implementation into a coherent session provider. Preserve native shared-command-queue behavior and kernel descriptor functionality, but remove superseded `IMetal*Capability` interfaces after callers migrate.

### CUDA

Implement CUDA directly against the generic session model. Report its actual execution mode accurately. If native CUDA is not yet implemented, the provider must identify itself as `Stub` or `Unavailable`, never `Native`. Remove superseded `ICuda*Capability` interfaces and nodes after migration.

When native CUDA support exists, distinguish compiler/toolkit availability, driver availability, compatible device availability, and successful session creation.

### SYCL

Implement SYCL directly against the generic session model. Do not label host-only or simulated behavior as native device acceleration. Remove superseded `ISycl*Capability` interfaces and nodes after migration.

### CPU reference session

Add a CPU reference session only where it improves generic operation testing or explicit fallback. Its descriptor must use a host/CPU identity rather than impersonating a GPU backend. If adding `BackendKind::CPU`, update all formatters, parsers, validators, configurations, and tests; numeric compatibility is not required.

### Provider tests

For every provider, test:

- descriptor truthfulness;
- session identity propagation;
- allocation, transfer, event, and release behavior supported by that provider;
- rejection of cross-session resources;
- native-required resolution failure for stub/simulated providers; and
- precise unavailable-hardware diagnostics.

Hardware-dependent tests must skip with a precise reason when the native runtime or compatible device is unavailable.

## Phase 4: Strengthen accelerator resource handles

The existing neutral tokens are valuable and should remain graph-facing contracts. Improve their safety incrementally.

Introduce opaque, reference-counted handles for:

- device allocations;
- host-visible or pinned allocations;
- execution queues or streams; and
- completion events.

Each handle must carry or reference:

- backend identity;
- execution mode;
- session identity;
- device identity;
- ownership and release semantics; and
- controlled access to backend-native handles through an extension mechanism.

Avoid exposing backend runtime classes in graph edge types.

Replacement requirements:

- Replace old serialized GPU token layouts and plugin ABI surfaces throughout the repository.
- Remove raw `void*` and integer handle fields once new handles are wired; do not retain deprecated mirror fields.
- Add validation rejecting mismatched backend, session, device, lease, queue, event, and ticket combinations.
- Eliminate double-release and leaked-allocation paths.
- Clarify whether graph tokens own resources or merely reference session-owned resources.

Add focused lifetime, copy/move, validation, and cross-session rejection tests.

## Phase 5: Generic infrastructure nodes

Implement backend-neutral nodes for common operations using `IAcceleratorSession`:

- `HostIngressNode` or repository-consistent equivalent;
- `H2DAsyncAccelNode`;
- `D2HAsyncAccelNode`;
- `LeaseReleaseAccelNode`; and
- queue/event synchronization only if a portable graph-level contract is well-defined.

These nodes should use coherent backend-neutral port contracts and carry domain sidecars without introducing backend-specific types. Existing port contracts may change when needed; update all graph configurations and callers together.

Requirements:

- Bind one selected session during initialization.
- Validate incoming resources against the bound session.
- Obtain queues and events from the session rather than inventing integer IDs.
- Use real native transfers when bound to a native session.
- Expose backend and execution-mode diagnostics.
- Never turn synthetic token creation into a native transfer claim.
- Delete superseded backend-specific infrastructure nodes, plugins, registrations, configurations, and tests once the generic replacements are integrated.

Use one common node implementation rather than copying transfer logic into every backend namespace. Do not create backend-named wrappers.

Add contract tests for the new generic transfer nodes across all providers. Remove tests that exist only to preserve legacy node contracts.

## Phase 6: Operation capability registry

Add an operation-level registry for domain algorithms. Domain nodes should request an algorithm implementation by intent rather than directly depending on `ICudaKernelCapability` or `IMetalKernelCapability`.

Model an operation request with at least:

- operation identifier and version;
- scalar/data types;
- tensor layout or problem shape;
- preferred/required backend;
- required execution mode;
- accuracy or feature requirements; and
- fallback policy.

Model an operation implementation with:

- supported descriptors and constraints;
- selected session;
- preparation or compilation step;
- execution method;
- output and synchronization contract;
- diagnostics; and
- optional backend-native extension data.

Do not build a portable kernel language or universal GPU DSL in this phase. Register compiled or backend-native operation implementations behind a common algorithm contract.

## Phase 7: DFT vertical slice

Use the DSP spectrum path as the first operation-level migration:

- CPU direct DFT implementation;
- existing Metal direct DFT implementation;
- native CUDA direct DFT implementation when native CUDA is available; and
- generic `SpectrumDftAccelNode` or repository-consistent equivalent.

The generic node must:

- preserve the established IQ input and magnitude output meaning;
- request a direct-DFT operation from the registry;
- preserve truthful DFT naming;
- report the selected provider, backend, execution mode, and device;
- reject a native-only request when only a stub or fallback is available;
- preserve domain metadata and sidecars; and
- keep CPU fallback explicit and configurable.

Replace backend-specific DFT graph nodes with the generic operation node after parity is established. Remove obsolete DFT plugins, registrations, configurations, and tests; keep reusable CPU and Metal algorithm implementations only behind the new operation contract.

Add correctness tests for:

- known-tone peak bin and magnitude;
- CPU/Metal/CUDA numerical parity within justified tolerances;
- plugin loading and dynamic construction;
- resolver selection;
- fallback policy;
- unavailable-backend diagnostics;
- session identity propagation; and
- proof that native-labeled execution used a native provider.

Use this vertical slice to validate the architecture before converting SAR or other domain algorithms.

## Phase 8: Resolver integration

Integrate session and operation selection with `ResolvingNodeProvider` without duplicating policy in several layers.

Define clear responsibility:

- the resolver chooses graph-level intent and coarse implementation family;
- the session resolver chooses a concrete runtime/device provider;
- the operation registry chooses an algorithm implementation compatible with the selected session and request; and
- the node executes the selected operation.

Support explicit policies such as:

- require native CUDA;
- prefer Metal, then native CUDA, then CPU;
- use any native accelerator;
- allow CPU fallback;
- disallow stubs and simulation; and
- select a specific device.

Diagnostics must show every considered provider, rejection reason, selected provider, and fallback decision.

Avoid configurations that map every backend intent to the same synthetic node while implying native execution.

## Phase 9: Controlled migration of remaining nodes

After the DFT vertical slice and generic transfer nodes pass, classify remaining nodes:

1. portable infrastructure operation;
2. portable domain operation;
3. backend-specific extension;
4. simulation/test fixture; or
5. obsolete duplicate.

Migrate in this order:

1. transform and reduce operations with clear contracts and test oracles;
2. domain algorithms with CPU references and representative fixtures;
3. reusable memory and synchronization operations;
4. peer copy and collectives only through optional extensions; and
5. deletion of superseded nodes, plugins, configuration forms, capability interfaces, and tests immediately after their in-repository callers migrate.

Do not create CPU analogues for inherently GPU-specific resource operations merely for naming symmetry.

## Error and diagnostics model

Replace ambiguous `bool` failures in new APIs with the repository's structured result/error style or a clean new result type.

Errors should identify:

- operation;
- backend and provider;
- execution mode;
- session and device;
- failing native/runtime stage;
- stable error category; and
- actionable diagnostic text.

Telemetry must distinguish:

- allocation;
- H2D, D2H, and D2D transfers;
- queue wait and synchronization;
- kernel or operation execution;
- initialization and compilation; and
- end-to-end graph time.

Zero-duration placeholder telemetry must not be presented as measured performance.

## Build and replacement requirements

- CPU-only builds must remain supported.
- Metal-only macOS builds must remain supported.
- CUDA/Jetson configurations must not inherit desktop-only architecture assumptions.
- SYCL-disabled builds must not require SYCL headers or libraries.
- Native runtimes must be compile-time gated and runtime validated.
- Stub backends must remain usable for contract tests but must be visibly marked as stubs.
- Replace the GPU plugin ABI as needed and update plugin tests to validate only the new ABI.
- Rewrite affected topology/configuration files and tests for the new schema; do not add compatibility mappings.
- Do not add circular dependencies between `libgraph`, `libgpu`, `libdsp`, or examples.

## Testing strategy

Add tests at four levels:

1. unit tests for descriptors, requests, resolution, handles, and validation;
2. provider contract tests shared across CPU/CUDA/Metal/SYCL implementations;
3. node tests for generic transfers and DFT operation behavior; and
4. graph/plugin integration tests exercising resolver, capability binding, execution, diagnostics, and cleanup.

Use typed or parameterized contract tests where practical so every backend provider is held to the same portable behavior without duplicating test bodies.

Native hardware tests must prove the selected implementation is native. Stub tests must prove that native-only resolution rejects them.

Run leak, repeated lifecycle, failure injection, and cross-session misuse tests for resource ownership.

## Acceptance gates

### Gate A: identity and session foundation

- execution modes are explicit and tested;
- sessions describe provider/device/runtime truthfully;
- native-required requests reject stubs and simulation;
- the new capability resolution and plugin integration are functional; and
- CPU-only and current Metal configurations build.

### Gate B: generic resource nodes

- generic H2D, D2H, and release nodes use a selected session;
- native sessions execute real native operations;
- synthetic implementations are labeled accurately;
- cross-session resources are rejected; and
- superseded infrastructure nodes and APIs have been removed.

### Gate C: DFT operation vertical slice

- the generic DFT node resolves CPU, Metal, and CUDA implementations according to policy;
- available native backends execute natively;
- numerical parity tests pass;
- fallback behavior is explicit and tested; and
- backend-specific DFT graph nodes have been removed after their implementations migrate behind the operation contract.

### Gate D: broader migration

- remaining nodes are classified before conversion;
- only semantically portable operations use generic interfaces;
- specialized features remain extensions; and
- no test fixture, stub, or synthetic path is reported as native acceleration.

## Final report

Finish with:

- files changed;
- design record and final architecture diagram;
- new interfaces and ownership rules;
- backend provider status matrix;
- migrated and deferred node inventory;
- deleted APIs, nodes, plugins, configurations, and replacement notes;
- build and test commands executed;
- test results and hardware-dependent skips;
- native/stub/simulated evidence for each backend;
- unresolved risks; and
- recommended next migration slice.

Complete Phases 0-7 and the associated acceptance gates. Implement Phases 8-9 only as far as they can be completed without weakening truth-in-labeling or test coverage. Do not claim architectural completion merely because interfaces exist: demonstrate the design through the generic transfer nodes and DFT vertical slice, and remove the superseded APIs instead of leaving two architectures behind.
```
