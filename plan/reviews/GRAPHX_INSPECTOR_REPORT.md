# GRAPHX Inspector Report

Date: 2026-07-05
Role source: plan/agents/GRAPHX_AGENT_ROLES.md
Scope: Current repository state only. No redesign or implementation.

## Inspection Method

- Reviewed active baseline and repository README.
- Inspected core runtime, builder, JSON loader, resolver, plugin bootstrap/registry.
- Inspected accelerator token contracts and GPU transport types.
- Inspected SAR canonical configs, SAR token flow nodes, and SAR tests.
- Inspected FHSS channelizer/decoder contracts and related test/build wiring.
- Inspected active test lane definitions and selected documentation surfaces.

## 1. Current Architecture Summary

### Architecture Findings

- Observed: GraphX is organized as a typed runtime core in libgraph, accelerator contracts/backends in libgpu, DSP/FHSS in libdsp, and SAR workflows in examples/SAR.
- Observed: The canonical integration path is GraphExecutorBuilder -> NodeProviderBootstrap -> GraphBuilder/JsonDynamicGraphLoader -> GraphExecutor.
- Observed: JSON graph loading plus plugin/provider resolution is the primary runtime composition model.
- Observed: Resolver-driven backend intent mapping is active (execution_backend, fallback policy, resolver mappings, resolver diagnostics).
- Inferred: The repository is operating as a shared platform with domain-heavy proving lanes (FHSS and SAR), not as a SAR-only runtime.
- Unknown: Cross-platform parity (Linux/macOS/Apple-native Metal runtime behavior) was not executed during this inspection.

### Implementation Defects

- Observed: None that block describing the current architecture shape.

## 2. Current Type and Packet Model

### Architecture Findings

- Observed: graph::gpu::accel::ControlToken<SidecarT> is the canonical accelerator-ready envelope.
- Observed: Domain identity is sidecar-owned; transport state is carried in lease/views/tickets with has_* presence flags.
- Observed: Compile-time token contracts are present via ControlTokenType, ControlTokenFor, and graph::AccelToken* concepts.
- Observed: SAR token alias is SarControlToken = ControlToken<SarPacket> with explicit comments forbidding identity decisions from host_ptr/ready_event.
- Observed: Test coverage exists for transport metadata not mutating sidecar identity (libgpu and SAR token contract tests).
- Inferred: Sidecar-first identity is currently an explicit cross-domain invariant for accelerator-ready paths.
- Unknown: Full-path sidecar invariants under every failure and cancellation mode were not exhaustively revalidated in this run.

### Implementation Defects

- Observed: None found in inspected token contracts that contradict sidecar-as-identity guidance.

## 3. Current Node and Port Model

### Architecture Findings

- Observed: Typed node model is active (NamedSourceNode, NamedInteriorNode, NamedSinkNode, TypedFixedFanNode and related typed port traits).
- Observed: Typed fixed fan helpers provide compile-time port tables, lifecycle integration, queue metrics, and transfer helpers.
- Observed: FHSS fixture channelizer is fixed at 64 outputs by type-level repetition and static_assert guardrails.
- Observed: No aggregate all-channels output packet type is present in inspected FHSS channelizer contracts.
- Inferred: The model intentionally favors compile-time shape guarantees over runtime port polymorphism for high-fanout FHSS fixtures.
- Unknown: Runtime memory pressure behavior for maximal fan-in/fan-out under prolonged load was not benchmarked.

### Implementation Defects

- Observed: None found that violate the current typed node/port contract in inspected files.

## 4. Current Token and Data Flow

### Architecture Findings

- Observed: SAR focused-image Metal canonical config uses OrderedCrsdSetInputSourceNode -> CrsdApertureAssemblyAdapterNode -> CrsdFocusedImageTransformMetalNode.
- Observed: Explicit resolver mappings exist for H2DAsyncAccelNode, SarBackprojectionTransformAccelNode, and D2HAsyncAccelNode in SAR Metal config.
- Observed: SAR H2D and D2H nodes explicitly propagate sidecar fields while treating host/device pointers and completion events as transport metadata.
- Observed: SAR backprojection accel node supports native-kernel-bound path and synthetic fallback path while preserving token-sidecar contract.
- Observed: CrsdFocusedImageTransformNode is CPU backprojection from assembled aperture frame.
- Observed: CrsdFocusedImageTransformMetalNode is labeled experimental/incomplete and includes explicit fallback and guardrail behavior.
- Observed: FHSS canonical config is an expanded deterministic fixture topology with explicit schedules and 64 channelized detector lanes.
- Inferred: SAR currently has one canonical named GPU config path, but algorithm maturity is still intentionally marked experimental in code and plugin metadata.
- Unknown: End-to-end throughput ceilings for FHSS and SAR canonical paths were not measured here.

### Implementation Defects

- Observed: None found that indicate encoded SAR identity transport through host_ptr/ready_event in inspected canonical path files.

## 5. Current Plugin/Provider and Resolver Flow

### Architecture Findings

- Observed: NodeProviderBootstrap supports multi-directory plugin loading, diagnostics counts, and provider lifecycle handle retention.
- Observed: PluginLoader performs ABI tag checks, plugin API version checks, symbol checks, and safe registration into PluginRegistry.
- Observed: PluginRegistry is mutex-protected and creates node instances via registered create functions and facades.
- Observed: ResolvingNodeProvider performs intent-to-concrete selection with backend preference ordering and fallback diagnostics.
- Observed: Resolver diagnostics are surfaced from GraphBuilder build results.
- Inferred: Resolver availability checks and mapping contracts are integrated deeply enough to be a first-class execution concern, not ancillary tooling.
- Unknown: Behavior with mixed plugin ABI ecosystems beyond inspected checks was not executed.

### Implementation Defects

- Observed: None found that break the current plugin/provider/resolver control flow in inspected code paths.

## 6. SDR/DSP/FHSS/SAR Capability Status

### Architecture Findings

- Observed: DSP includes CPU direct DFT and Metal direct DFT paths; naming guardrails avoid FFT claims where FFT is not implemented.
- Observed: FHSS path is clearly labeled fixture-only deterministic CPU lane with CPSM/Viterbi decode path and explicit non-production claims.
- Observed: FHSS channelizer is explicitly documented as fixture mixer/decimator, not production filter-bank separation.
- Observed: SAR includes CRSD ordered-set ingest, aperture assembly adapter, CPU focused-image transform, Metal-focused-image node, sinks, and local-only baseline tooling.
- Observed: SAR local-only lanes and external baseline tools are explicitly gated in test/build configuration.
- Inferred: Truth-in-labeling policy is actively encoded in code comments, plugin descriptions, test names, and CMake labels.
- Unknown: Production RF/SAR claims are intentionally out-of-scope for current capabilities.

### Implementation Defects

- Observed: None contradictory to declared fixture/local-only/experimental status in inspected DSP/FHSS/SAR sources.

## 7. GPU/Accelerator Readiness Status

### Architecture Findings

- Observed: Backend-neutral accelerator types (views, leases, transfer tickets, kernel tickets, collectives) are established in libgpu.
- Observed: Metal capability interfaces and native runtime components exist (context, transfer, kernel, telemetry, descriptors).
- Observed: SAR accel nodes rely on explicit tokenized transport contracts and update sidecar queue/timing counters.
- Observed: SAR Metal focused-image node can execute native capability path but still emits incomplete algorithm warning status.
- Inferred: Accelerator plumbing is significantly present; domain algorithm parity is not uniformly complete.
- Unknown: CUDA domain-path completeness relative to Metal-first workflows was not fully audited in this pass.

### Implementation Defects

- Observed: No immediate contract-level defect found in inspected accelerator envelope and SAR token plumbing.

## 8. C++26 Usage Observations

### Architecture Findings

- Observed: Concepts are actively used for token and port contracts.
- Observed: std::expected-based error channels are used across builder/executor/bootstrap/loader paths.
- Observed: Strong typing and compile-time checks (static_assert/type traits) are used in FHSS and accelerator contracts.
- Observed: Ownership is mostly explicit with shared_ptr/unique_ptr and value structs for tokens/diagnostics.
- Inferred: Current style blends modern C++26 patterns with legacy-style dynamic plugin ABI surfaces.
- Unknown: Compiler-specific C++26 feature portability beyond configured lanes was not verified.

### Implementation Defects

- Observed: Doxygen-style auto-generated comment noise is pervasive in several core headers/sources, increasing cognitive load but not changing runtime semantics.

## 9. Complexity Hotspots and Obsolete Abstractions

### Architecture Findings

- Observed: SAR test CMake is very large with many compile definitions, ownership lanes, and local-only gating paths.
- Observed: FHSS canonical graph remains mechanically expansive (64 detector lanes) by design.
- Observed: Resolver + plugin/provider + JSON validation is a deep stack with multiple diagnostics layers.
- Inferred: Most current complexity appears intentional for guardrails and determinism, not accidental layering.
- Unknown: Whether any archived abstractions still have active runtime influence outside inspected files.

### Implementation Defects

- Observed: Documentation staleness in doc/README.md: it lists plan/agents/GRAPHX_PR_AGENTS.md as active, but that file is currently only present under archive.
- Observed: Comment-generation verbosity in core files (duplicate/boilerplate method docs) is a maintainability burden and obscures signal in reviews.

## 10. Test and Documentation Coverage Gaps

### Architecture Findings

- Observed: Test ownership lanes are explicit for SAR (CRSD I/O, nodes, runtime integration, local-only) and for libdsp unit coverage.
- Observed: Local-only tests are clearly labeled/gated; CI-safe lanes are explicitly marked.
- Observed: SAR/FHSS truth-in-labeling and guardrail tests are present across multiple suites.
- Inferred: Contract/guardrail coverage is strong where policy is explicit (token semantics, local-only gating, fixture labeling).
- Unknown: Unified cross-domain performance instrumentation completeness was not established from inspected tests alone.

### Implementation Defects

- Observed: Active-vs-archived documentation boundaries are mostly clear, but at least one active-doc pointer is stale (doc/README.md reference above).

## Summary Snapshot

- Observed: Repository currently aligns with a typed, plugin-driven, resolver-aware GraphX runtime and accelerator token contract model.
- Observed: FHSS is deterministic fixture CPU-first; SAR has canonical CPU and one explicitly experimental Metal-focused path.
- Observed: No inspected evidence of redesign drift away from explicit token sidecar identity semantics.
- Observed: Primary non-architectural issues found are maintainability/documentation quality (stale active reference and heavy auto-doc noise), not core contract breakage.
