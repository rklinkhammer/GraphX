# SAR2 Architect Report

Date: 2026-06-10
Scope: Current repository state only
Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## 1) Current Type Model

- Observed: the canonical SAR runtime contract is `AccelControlToken<SarSidecar>` in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
- Observed: `SarSidecar` carries SAR-facing identity, telemetry, merge state, and stage timing data used by the runtime path.
- Observed: `AccelControlToken<SidecarT>` still carries generic GPU transport fields plus `has_*` flags.
- Observed: helper structs still exist alongside the token model, including `SarMessageEnvelope`, `SarBufferDescriptor`, `SarGpuMetadata`, `SarDiagnosticsSnapshot`, and `SarStageTimingMetrics`.
- Inferred: the runtime is centered on the sidecar alias, but transport helpers still add overlapping surface area.

## 2) Current Node Model

- Observed: the main SAR chain is source → range stage → split → H2D → backprojection → D2H → merge → diagnostics sink.
- Observed: `SyntheticApertureIqSourceNode` and `GotchaReplaySourceNode` are the current source nodes.
- Observed: `SarMaterializedImageSinkNode` is the deterministic in-memory sink used for parity and CI-safe validation.
- Observed: `SarVisualizationSinkNode` still exists as a file-artifact sink.
- Observed: `SarBackprojectionTransformAccelNode` remains the accel/native wrapper around backprojection.
- Observed: `sar_example` and `sar_benchmark` are still built from [examples/SAR/src/main.cpp](examples/SAR/src/main.cpp) and [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp).

## 3) Current Token / Data Flow

- Observed: source nodes emit `SarAccelControlToken` values and populate `host_view` plus `SarSidecar` fields.
- Observed: `H2DAsyncNode` validates the host view, synthesizes a device view, records transfer tickets, and writes H2D queue/timing data into the sidecar.
- Observed: `SarBackprojectionTransformAccelNode` validates the device view, synthesizes or forwards device output, records a kernel ticket, and writes kernel queue/timing data into the sidecar.
- Observed: `D2HAsyncNode` validates the device view, synthesizes a host view, records a transfer ticket, and writes D2H queue/timing data into the sidecar.
- Observed: `ImageTileMergeNode` accumulates merge state and timing into the sidecar.
- Observed: `SarDiagnosticsSinkNode` consumes sidecar state and `GraphMetrics` to produce the final diagnostics snapshot.
- Observed: `host_ptr` remains an opaque synthetic pointer sentinel in source and transfer paths.
- Observed: `ready_event` remains present in device-view handling and tests as an opaque synthetic event field.

## 4) Resolver / Substitution Flow

- Observed: graph construction still goes through `GraphExecutorBuilder`, JSON topology config, and plugin directories.
- Observed: runtime node resolution is wrapper-based through `NodeFacadeAdapterWrapper::GetType()` and `GetNode<T>()`.
- Observed: `sar_benchmark.cpp` explicitly resolves either `SarBackprojectionTransformNode` or `SarBackprojectionTransformAccelNode` at runtime.
- Observed: the diagnostics sink is resolved the same way in [examples/SAR/src/main.cpp](examples/SAR/src/main.cpp), [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp), and multiple tests.
- Inferred: dynamic loading and resolver behavior remain GraphX-native rather than external-package-shaped.

## 5) Violations of the Canonical Token Story

- Observed: the canonical `AccelControlToken<SarSidecar>` contract already exists.
- Observed: `host_ptr` and `ready_event` still participate in transport semantics instead of being fully removed as identity carriers.
- Observed: `SarMessageEnvelope`, `SarBufferDescriptor`, and `SarGpuMetadata` duplicate parts of the transport story beside the canonical token model.
- Inferred: the token alias is canonical, but the surrounding transport representation still carries redundant detail.

## 6) Obsolete or Dormant Abstractions

- Observed: `SarMessageEnvelope` has no in-repo call sites beyond its declaration.
- Observed: `SarBufferDescriptor` has no in-repo call sites beyond its declaration.
- Observed: `SarGpuMetadata` has no in-repo call sites beyond its declaration.
- Inferred: these appear dormant or transitional relative to the sidecar-backed runtime path.

## 7) Complexity Hotspots

- Observed: `sar_benchmark.cpp` is the largest integration surface and combines resolver logic, timing collection, trace emission, and device-reduce evaluation.
- Observed: repeated `ElapsedUs(...)` helpers exist across multiple SAR node source files.
- Observed: `ResolveDiagnosticsSink(...)` is duplicated across [examples/SAR/src/main.cpp](examples/SAR/src/main.cpp), [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp), and multiple SAR tests.
- Observed: the SAR example now carries layered validation artifacts: benchmark tracing, external-baseline policy and registry, comparator tooling, replay guide, tiny fixture, and a bounded CI lane.
- Inferred: the largest complexity cost is helper duplication and layered validation artifacts, not the canonical token alias itself.

## 8) Blockers and Constraints

- Observed: there is no structural blocker to `AccelControlToken<SarSidecar>` itself; it is already the public SAR runtime contract.
- Observed: the main remaining blocker to a cleaner identity-free story is continued use of `host_ptr` and `ready_event` as transport fields.
- Unknown: whether the repository intends to remove those fields or keep them as opaque transport metadata.

## 9) Existing External Comparison / Baseline Hooks

- Observed: the external baseline policy exists at [plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md](plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md).
- Observed: the machine-readable registry exists at [plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json](plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json).
- Observed: the registry marks SarPy, ISCE3, and gotcha-back as comparator-only packages and preserves the GraphX runtime contract.
- Observed: `test_external_baseline_policy_registry.cpp` validates the policy and registry contract.
- Observed: `sar_benchmark.cpp` emits structured trace output and the trace-schema test validates that contract.
- Observed: [examples/SAR/BENCHMARK_REPORT.md](examples/SAR/BENCHMARK_REPORT.md) documents the CI-safe benchmark profile and the local-only larger profile.
- Observed: [examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh](examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh) compares `sar_example` under stub and Metal variants.
- Observed: `main.cpp` reports runtime completion and diagnostics queue/backpressure fields.

## 10) Overall State

- Observed: the repository working tree is clean.
- Observed: the SAR test target already includes the replay guide, tiny fixture, comparator, and bounded CI lane coverage.
- Inferred: the repository currently has a stable, layered validation story with the canonical SAR token path in place and some cleanup pressure around transport helpers and repeated resolver/timing helpers.

Stop after the current-state report.
