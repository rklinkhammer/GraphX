# SAR2 Simplifier Report

Date: 2026-06-10
Scope: Current repository state only
Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## 1) Current Type Model

- Observed: the canonical SAR runtime contract is `AccelControlToken<SarSidecar>` in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
- Observed: `SarSidecar` already carries the SAR-facing identity and telemetry fields used by the runtime path: sequence/batch/aperture IDs, pulse range, stream/tile/backend IDs, marker, payload size, queue IDs, tile counters, byte counters, timing counters, watermark state, merge completion, and stage timings.
- Observed: `AccelControlToken<SidecarT>` still carries generic GPU transport state (`lease`, `device_view`, `host_view`, `transfer_ticket`, `kernel_ticket`) plus `has_*` booleans.
- Observed: helper structs remain alongside the canonical token model, including `SarMessageEnvelope`, `SarBufferDescriptor`, `SarGpuMetadata`, `SarDiagnosticsSnapshot`, and `SarStageTimingMetrics`.
- Inferred: the repository is already centered on the sidecar-backed token alias, but the transport layer still exposes extra helper types that overlap conceptually with the canonical token.

## 2) Current Node Model

- Observed: the main SAR chain is source → range stage → split → H2D → backprojection → D2H → merge → diagnostics sink.
- Observed: `SyntheticApertureIqSourceNode` and `GotchaReplaySourceNode` are the current source nodes.
- Observed: `SarMaterializedImageSinkNode` is the deterministic in-memory capture sink used for image parity and CI-safe validation.
- Observed: `SarVisualizationSinkNode` still exists as a file-artifact sink.
- Observed: `SarBackprojectionTransformAccelNode` remains the accel/native wrapper around backprojection.
- Observed: `sar_example` is built from [examples/SAR/src/main.cpp](examples/SAR/src/main.cpp), and `sar_benchmark` is built from [examples/SAR/src/sar_benchmark.cpp](examples/SAR/src/sar_benchmark.cpp).

## 3) Current Token / Data Flow

- Observed: the source nodes emit `SarAccelControlToken` values and populate `host_view` plus `SarSidecar` fields.
- Observed: `H2DAsyncNode` validates the host view, synthesizes a device view, records transfer tickets, and writes H2D queue/timing data into the sidecar.
- Observed: `SarBackprojectionTransformAccelNode` validates the device view, synthesizes or forwards device output, records a kernel ticket, and writes kernel queue/timing data into the sidecar.
- Observed: `D2HAsyncNode` validates the device view, synthesizes a host view, records a transfer ticket, and writes D2H queue/timing data into the sidecar.
- Observed: `ImageTileMergeNode` accumulates merge state and timing into the sidecar.
- Observed: `SarDiagnosticsSinkNode` consumes sidecar state and `GraphMetrics` to produce the final diagnostics snapshot.
- Observed: `host_ptr` is still used as an opaque synthetic pointer sentinel in source and transfer paths.
- Observed: `ready_event` is still present in device-view handling and tests as an opaque synthetic event field.

## 4) Resolver / Substitution Flow

- Observed: graph construction still goes through `GraphExecutorBuilder`, JSON topology config, and plugin directories.
- Observed: runtime node resolution remains wrapper-based through `NodeFacadeAdapterWrapper::GetType()` and `GetNode<T>()`.
- Observed: `sar_benchmark.cpp` explicitly resolves either `SarBackprojectionTransformNode` or `SarBackprojectionTransformAccelNode` at runtime.
- Observed: the diagnostics sink is resolved the same way in `main.cpp` and multiple SAR tests.
- Inferred: dynamic loading and resolver behavior remain GraphX-native rather than external-package-shaped.

## 5) Accel-Token Surface

- Observed: the canonical `AccelControlToken<SarSidecar>` alias is already in place and asserted in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
- Observed: the surrounding transport state is still duplicated between the canonical token and helper structs such as `SarGpuMetadata`.
- Observed: `host_ptr` and `ready_event` remain part of the transport boundary and are still used by SAR source/transfer tests.
- Inferred: the token alias is canonical, but the surrounding transport representation still carries redundant detail.

## 6) Obsolete / Dormant Abstractions

- Observed: `SarMessageEnvelope` has no in-repo call sites beyond its declaration.
- Observed: `SarBufferDescriptor` has no in-repo call sites beyond its declaration.
- Observed: `SarGpuMetadata` has no in-repo call sites beyond its declaration.
- Inferred: these look dormant or transitional relative to the sidecar-backed runtime path.

## 7) Complexity Hotspots

- Observed: `sar_benchmark.cpp` is the largest integration surface and combines resolver logic, timing collection, trace emission, and device-reduce evaluation.
- Observed: repeated `ElapsedUs(...)` helpers exist across multiple SAR node source files.
- Observed: `ResolveDiagnosticsSink(...)` is duplicated across `main.cpp`, `sar_benchmark.cpp`, and many SAR tests.
- Observed: the SAR example now carries layered validation artifacts: benchmark tracing, external-baseline policy/registry, comparator tooling, replay guide, tiny fixture, and bounded CI lane.
- Inferred: the highest complexity cost is helper duplication and layered validation artifacts, not the canonical token alias itself.

## 8) Simplification Pressure

- Observed: the repository already has one canonical SAR runtime contract, but it still exposes helper types and opaque transport sentinels around that contract.
- Observed: there is clear repetition in resolver and elapsed-time helper code across nodes, benchmark code, and tests.
- Observed: the current SAR validation stack is intentionally layered and test-heavy.
- Unknown: whether the helper structs and opaque transport fields are intended to remain stable or are candidates for eventual cleanup.

## 9) Current External Comparison / Baseline Hooks

- Observed: the external baseline policy exists at [plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md](plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md).
- Observed: the registry exists at [plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json](plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json).
- Observed: the registry marks SarPy, ISCE3, and gotcha-back as comparator-only packages and preserves the GraphX runtime contract.
- Observed: `test_external_baseline_policy_registry.cpp` validates the policy/registry contract.
- Observed: `sar_benchmark.cpp` emits structured trace output and the trace-schema test validates that contract.
- Observed: [examples/SAR/BENCHMARK_REPORT.md](examples/SAR/BENCHMARK_REPORT.md) documents the CI-safe benchmark profile and the local-only larger profile.
- Observed: [examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh](examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh) compares `sar_example` under stub and Metal variants.
- Observed: `main.cpp` reports runtime completion and diagnostics queue/backpressure fields.

## 10) Current State Summary

- Observed: the repository working tree is currently clean.
- Observed: the SAR example test target includes the replay guide, tiny fixture, comparator, and bounded CI lane coverage.
- Inferred: the repository has a layered validation story that is stable and test-backed.

Stop after the current-state report.