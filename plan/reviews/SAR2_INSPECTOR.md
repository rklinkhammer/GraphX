# SAR2 Inspector Report

Date: 2026-06-10
Scope: Current repository state only
Role source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`

## 1) Current Type Model

- Observed: the canonical SAR runtime contract is `AccelControlToken<SarSidecar>` in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
- Observed: `SarSidecar` carries the SAR-facing identity, telemetry, and merge state: sequence/batch/aperture IDs, pulse range, stream/tile/backend IDs, marker, payload size, queue IDs, tile counters, byte counters, timing counters, watermark state, merge completion, and stage timings.
- Observed: `AccelControlToken<SidecarT>` still retains generic GPU transport metadata (`lease`, `device_view`, `host_view`, `transfer_ticket`, `kernel_ticket`) plus `has_*` booleans.
- Observed: helper structs remain beside the canonical token model, including `SarMessageEnvelope`, `SarBufferDescriptor`, `SarGpuMetadata`, `SarDiagnosticsSnapshot`, and `SarStageTimingMetrics`.
- Inferred: the runtime is sidecar-first, but the transport layer still exposes older and partially overlapping helper abstractions.

## 2) Current Node Model

- Observed: the primary SAR chain is source → range stage → split → H2D → backprojection → D2H → merge → diagnostics sink.
- Observed: `SyntheticApertureIqSourceNode` and `GotchaReplaySourceNode` are the main source nodes.
- Observed: `SarMaterializedImageSinkNode` provides deterministic in-memory materialization for image parity and CI-safe validation.
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
- Observed: `host_ptr` remains an opaque synthetic pointer sentinel in source and transfer paths.
- Observed: `ready_event` remains present in device-view handling and tests as an opaque synthetic event field.

## 4) Resolver / Substitution Flow

- Observed: graph construction still goes through `GraphExecutorBuilder`, JSON topology config, and plugin directories.
- Observed: runtime node resolution is wrapper-based, using `NodeFacadeAdapterWrapper::GetType()` and `GetNode<T>()`.
- Observed: `sar_benchmark.cpp` explicitly resolves either `SarBackprojectionTransformNode` or `SarBackprojectionTransformAccelNode` at runtime.
- Observed: the diagnostics sink is resolved the same way in multiple SAR tests and in `main.cpp`.
- Inferred: dynamic loading and resolver behavior remain GraphX-native rather than external-package-shaped.

## 5) Violations of Accel-Token Architecture

- Observed: the canonical `AccelControlToken<SarSidecar>` contract exists and is asserted in [examples/SAR/include/sar/SarMessages.hpp](examples/SAR/include/sar/SarMessages.hpp).
- Observed: `host_ptr` and `ready_event` still participate in transport semantics instead of being fully removed as identity carriers.
- Observed: `SarMessageEnvelope`, `SarBufferDescriptor`, and `SarGpuMetadata` duplicate parts of the transport story beside the canonical token model.
- Inferred: the architecture is canonical at the token alias level, but the surrounding transport helpers still add semantic surface area.

## 6) Obsolete Abstractions

- Observed: `SarMessageEnvelope` has no in-repo call sites beyond its header definition.
- Observed: `SarBufferDescriptor` has no in-repo call sites beyond its header definition.
- Observed: `SarGpuMetadata` has no in-repo call sites beyond its header definition.
- Inferred: these appear to be dormant or transitional abstractions relative to the sidecar-backed token model.

## 7) Complexity Hotspots

- Observed: `sar_benchmark.cpp` is the largest integration surface and combines resolver logic, timing collection, trace emission, and device-reduce evaluation.
- Observed: repeated `ElapsedUs(...)` helpers exist across multiple SAR node source files.
- Observed: `ResolveDiagnosticsSink(...)` is duplicated across `main.cpp`, `sar_benchmark.cpp`, and many SAR tests.
- Observed: the SAR example now carries multiple validation layers: benchmark tracing, external-baseline policy/registry, image comparator tooling, replay guide, tiny fixture, and a bounded CI lane.
- Inferred: the biggest complexity cost is helper duplication and layered validation artifacts, not the canonical token alias itself.

## 8) Blockers for `AccelControlToken<SarSidecar>`

- Observed: there is no structural blocker to the alias itself; it is already the public SAR runtime contract.
- Observed: the remaining blocker to a cleaner identity-free story is the continued use of `host_ptr` and `ready_event` as transport fields.
- Unknown: whether the repo intends to remove those fields or keep them as opaque transport metadata.

## 9) Existing External Comparison / Baseline Hooks

- Observed: the external baseline policy exists at [plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md](plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md).
- Observed: the machine-readable registry exists at [plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json](plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.json).
- Observed: the registry marks SarPy, ISCE3, and gotcha-back as comparator-only packages and preserves the GraphX runtime contract.
- Observed: `test_external_baseline_policy_registry.cpp` validates the policy/registry contract.
- Observed: `sar_benchmark.cpp` emits structured trace output and the trace-schema test validates that contract.
- Observed: the benchmark report at [examples/SAR/BENCHMARK_REPORT.md](examples/SAR/BENCHMARK_REPORT.md) describes the CI-safe profile and local-only larger profile.
- Observed: [examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh](examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh) compares `sar_example` under stub and Metal variants.
- Observed: `main.cpp` reports runtime completion and diagnostics queue/backpressure fields.

## 10) Current State Summary

- Observed: the repository is currently clean in the working tree.
- Observed: the SAR example test target now includes the replay guide, tiny fixture, comparator, and bounded CI lane coverage.
- Inferred: the repository has a layered validation story that is currently stable and test-backed.

Stop after analysis.