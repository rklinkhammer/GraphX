# SAR Inspector Report

Scope: Current repository state only. No redesign. No implementation.

Role: `INSPECTOR` per `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

## 1. Current Type Model

- Observed: SAR's canonical accel-token type is `SarAccelControlToken`, an alias of `AccelControlToken<SarSidecar>` in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: SAR identity now lives in `SarSidecar`: sequence, batch, aperture, pulse range, stream, tile, backend, marker, byte counts, merge counters, and stage timings.
- Observed: Transport fields such as `host_view.host_ptr` and `device_view.ready_event` are explicitly documented and tested as opaque transport metadata, not SAR identity.
- Observed: Legacy SAR payload names are rejected under `edge_contract: accel-token` by `GraphConfigParser`.

## 2. Current Node Model

- Observed: SAR nodes now expose `SarAccelControlToken` through the active flow: source, range/window/compression stages, azimuth split, H2D, backprojection, D2H, merge, diagnostics.
- Observed: Legacy public node names remain as aliases:
  - `H2DAsyncNode -> H2DAsyncAccelNode`
  - `D2HAsyncNode -> D2HAsyncAccelNode`
  - `SarBackprojectionTransformNode -> SarBackprojectionTransformAccelNode`
- Observed: The aliases preserve old graph config names while the C++ token surface is now SAR accel-token based.
- Inferred: The alias layer is compatibility scaffolding rather than an independent runtime path.

## 3. Current Token/Data Flow

- Observed: `SyntheticApertureIqSourceNode` creates `SarAccelControlToken` instances with populated sidecar identity and host views.
- Observed: `AzimuthTileSplitNode` fans out SAR accel tokens by tile, updating sidecar tile metadata.
- Observed: `H2DAsyncAccelNode` consumes host views, synthesizes or records device view state, updates transfer ticket metadata, and preserves sidecar identity.
- Observed: `SarBackprojectionTransformAccelNode` consumes device views, uses native Metal when configured and available, otherwise follows the synthetic accel path.
- Observed: `D2HAsyncAccelNode` consumes device views and produces host views while preserving sidecar identity.
- Observed: `ImageTileMergeNode` merges host-view tiles and updates sidecar merge statistics, byte totals, timing, and completion state.

## 4. Resolver Substitution Flow

- Observed: `examples/SAR/config/sar_stripmap_definitive.json` declares:
  - `execution_backend: auto`
  - `backend_fallback_policy: strict`
  - `resolver_diagnostics: true`
  - `edge_contract: accel-token`
- Observed: SAR config maps `SarBackprojectionTransformNode` through resolver mappings, but H2D/D2H are still represented by compatibility names.
- Observed: Default resolver mappings in `NodeResolutionRegistry` are generic GPU-oriented, using `HostPinnedBufferView` and `DeviceBufferView` labels for H2D/D2H/DeviceKernel-style nodes.
- Observed: `ResolvingNodeProvider` uses backend preference order `metal`, `sycl`, `stub`, `cuda` for `auto`.
- Inferred: SAR accel-token compatibility currently depends on wrapper aliases and SAR-specific tests/config discipline around generic resolver contracts.

## 5. Violations Of Accel-Token Architecture

- Observed: No active SAR node flow was found using legacy SAR payload message types as the runtime payload under `edge_contract: accel-token`.
- Observed: Parser guardrails reject legacy SAR payload contracts in accel-token graphs.
- Observed: Tests assert `host_ptr` and `ready_event` are transport-only and do not define SAR identity.
- Observed: Some nodes still synthesize local opaque host pointers, device pointers, and event IDs.
- Inferred: These synthetic transport fields are compatible with the documented architecture, but they remain easy places for future accidental identity coupling.

## 6. Obsolete Abstractions

- Observed: Legacy message names still exist as rejected contract strings and historical vocabulary in tests/docs.
- Observed: Old node names remain as aliases for compatibility.
- Observed: Generic resolver token labels such as `HostPinnedBufferView` and `DeviceBufferView` still appear in resolver mappings even though SAR runtime payloads are now `SarAccelControlToken`.
- Inferred: The repository is mid-transition: runtime token architecture is mostly migrated, while naming, resolver vocabulary, and compatibility aliases retain pre-accel-token concepts.

## 7. Complexity Hotspots

- Observed: `examples/SAR/src/sar_benchmark.cpp` is a large mixed-purpose benchmark/diagnostics/baseline harness with lifecycle tracing, graph-vs-direct comparison, and reporting logic.
- Observed: Opaque pointer/event generation remains duplicated across SAR nodes, although elapsed-time measurement is now consolidated in `SarRuntimeHelpers.hpp`.
- Observed: `ImageTileMergeNode` combines merge semantics, diagnostics aggregation, transfer/kernel ticket synthesis, ordering checks, and sidecar finalization.
- Observed: Resolver behavior spans graph config JSON, `GraphConfigParser`, `NodeResolutionRegistry`, `ResolvingNodeProvider`, wrapper aliases, and SAR tests.
- Inferred: The highest comprehension cost is now resolver/token vocabulary alignment, not the core `SarAccelControlToken` data model.

## 8. Blockers For `AccelControlToken<SarSidecar>`

- Observed: The core alias already exists and is used by SAR accel nodes.
- Observed: Transport opacity is documented and covered by tests.
- Observed: Legacy payload contracts are rejected under accel-token mode.
- Observed: Remaining blockers are not the absence of `AccelControlToken<SarSidecar>`, but surrounding consistency issues:
  - generic resolver contract labels still describe view-level payloads,
  - compatibility aliases preserve older node names,
  - synthetic transport helpers remain scattered,
  - external baseline execution is not fully wired into CI.

## 9. Existing External Comparison/Baseline Hooks

- Observed: External baseline policy and package registry files exist under `plan/reviews`.
- Observed: Gotcha-back adapter and image comparator tooling exist under `examples/SAR/tools`.
- Observed: CI fixtures and RRP tests reference gotcha-back-style contracts.
- Observed: Current CI-safe replay uses deterministic generated reference imagery rather than executing an external gotcha-back binary.
- Observed: No current repository hook was found for OpenSAR or OpenSARLab execution.
- Observed: SarPy and ISCE3 are present as registered comparator candidates, but no active SAR example test appears to execute them.

Tests were not run for this inspection-only report.
