# SAR Inspector Report (Current Repository State)

Scope: Current repository state only. No redesign, no implementation.
Role contract source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

## 1. Current Type Model

- Observed: Canonical SAR accel token alias exists: `SarAccelControlToken = AccelControlToken<SarSidecar>` in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `SarSidecar` is explicit identity/metadata carrier (sequence, batch, aperture, pulse range, stream, tile, backend, marker, payload size, queue IDs, stage timings) in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `AccelControlToken<SidecarT>` carries sidecar plus accel transport/control fields (`lease`, `device_view`, `host_view`, `transfer_ticket`, `kernel_ticket`, presence flags).
- Observed: Legacy SAR payload/status types remain defined and used in pipeline boundaries:
  - `SarPulseBlockMessage` (source/range stages)
  - `SarMergeStatusMessage` (merge output)
  - `SarDiagnosticsMessage` (sink diagnostics)
- Observed: Additional envelope/gpu structs remain (`SarMessageEnvelope`, `SarBufferDescriptor`, `SarGpuMetadata`, `SarDispatchMetadata`).
- Inferred: Type surface is mixed: canonical token type exists, but non-token SAR message families are still runtime-significant.

## 2. Current Node Model

- Observed: Definitive JSON topology in `examples/SAR/config/sar_stripmap_definitive.json` wires:
  `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncNode -> SarBackprojectionTransformNode -> D2HAsyncNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- Observed: Stage contracts are mixed by implementation:
  - `SyntheticApertureIqSourceNode`, `RangeWindowNode`, `RangeCompressionNode` use `SarPulseBlockMessage`.
  - `AzimuthTileSplitNode` converts `SarPulseBlockMessage` to `SarAccelControlToken`.
  - `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, `D2HAsyncAccelNode` operate on `SarAccelControlToken`.
  - `ImageTileMergeNode` consumes token, emits `SarMergeStatusMessage`.
  - `SarDiagnosticsSinkNode` consumes `SarMergeStatusMessage` and emits internal `SarDiagnosticsMessage` state.
- Observed: Alias wrappers map intent names to accel node implementations:
  - `H2DAsyncNode -> H2DAsyncAccelNode`
  - `D2HAsyncNode -> D2HAsyncAccelNode`
  - `SarBackprojectionTransformNode -> SarBackprojectionTransformAccelNode`
  (headers in `examples/SAR/include/sar`).
- Observed: Materialization/visualization are token-path interiors:
  - `SarMaterializedImageSinkNode` consumes/emits token.
  - `SarVisualizationSinkNode` consumes/emits token.

## 3. Current Token/Data Flow

- Observed: Identity in split is sourced from message envelope into sidecar; token id is opaque monotonic counter in `examples/SAR/src/AzimuthTileSplitNode.cpp`.
- Observed: `host_view.host_ptr` in split and D2H is constant opaque sentinel (`0x1`) via `OpaqueHostPointer()` in `examples/SAR/src/AzimuthTileSplitNode.cpp` and `examples/SAR/src/D2HAsyncAccelNode.cpp`.
- Observed: H2D/backprojection simulated paths synthesize `device_ptr` from bytes + sequence counters (`MakeSyntheticDevicePointer`) in `examples/SAR/src/H2DAsyncAccelNode.cpp` and `examples/SAR/src/SarBackprojectionTransformAccelNode.cpp`.
- Observed: Simulated H2D/backprojection set `device_view.ready_event = 0`; completion is carried by opaque transfer/kernel ticket completion events (`NextOpaqueEventId`).
- Observed: Merge derives output envelope from sidecar first and only falls back to provided sequence/stream values when sidecar fields are zero (`ImageTileMergeNode::BuildStatusMessage`).
- Observed: Token sidecar timings are accumulated across stages; merge and diagnostics preserve/report these fields.
- Observed: Tests explicitly enforce sidecar-identity invariance to host pointer and ready-event mutation in `examples/SAR/test/test_sar_accel_nodes.cpp`.
- Inferred: Runtime identity transport path is sidecar-centric; pointer/event channels are treated as transport telemetry/control artifacts.

## 4. Resolver Substitution Flow

- Observed: Parser-level resolver fields are validated in `libgraph/src/graph/GraphConfigParser.cpp`:
  - `execution_backend` in `{auto, metal, cuda, sycl, stub}`
  - `backend_fallback_policy` in `{strict, allow_fallback}`
  - `resolver_diagnostics` boolean
  - `edge_contract` currently constrained to `accel-token` when set.
- Observed: Legacy SAR payload contracts are explicitly rejected when `edge_contract == "accel-token"` via `IsLegacySarPayloadContract` + validation path in `GraphConfigParser.cpp`.
- Observed: Definitive SAR config includes resolver mapping for `SarBackprojectionTransformNode` variants; H2D/D2H are not mapped in the definitive JSON by default.
- Observed: Runtime tests add temporary H2D/D2H mappings and validate composed-provider resolution in `examples/SAR/test/test_sar_json_runtime.cpp`.
- Observed: Plugin packaging includes SAR nodes under `examples/SAR/plugins/CMakeLists.txt`; runtime can also load additional plugin directories in `examples/SAR/src/main.cpp`.

## 5. Violations of Accel-Token Architecture

- Observed: Pipeline is not single-contract end-to-end; upstream DSP/source path still uses `SarPulseBlockMessage` before token conversion at split.
- Observed: Merge/sink boundary exits token contract (`SarMergeStatusMessage` then `SarDiagnosticsMessage`).
- Observed: Side-channel payload store exists (`SarAccelTokenImagePayloadStore` global mutex + map keyed by token id), written in backprojection and consumed by materialized sink.
- Inferred: Side-channel payload store introduces an alternate data path outside explicit edge payload contracts for materialized-image behavior.
- Unknown: Whether side-channel materialization path is considered acceptable runtime architecture or temporary instrumentation scaffolding.

## 6. Obsolete Abstractions

- Observed: Legacy SAR payload contract names are intentionally retained in parser as rejection literals (guardrails), not runtime contracts.
- Observed: Deprecated config files remain (`sar_stripmap_definitive_nonmetal.json`, `sar_stripmap_definitive_metal.json`) with deprecation notices.
- Observed: Legacy-named message families remain in type model and stage APIs (`SarPulseBlockMessage`, `SarMergeStatusMessage`, `SarDiagnosticsMessage`).
- Inferred: Some legacy abstractions are intentionally retained for guardrails/tests/document continuity, not necessarily active contract direction.

## 7. Complexity Hotspots

- Observed: Duplicate local `ElapsedUs` helper appears across many SAR source files:
  `AzimuthTileSplitNode.cpp`, `H2DAsyncAccelNode.cpp`, `SarBackprojectionTransformAccelNode.cpp`, `D2HAsyncAccelNode.cpp`, `ImageTileMergeNode.cpp`, `RangeWindowNode.cpp`, `RangeCompressionNode.cpp`, `SarDiagnosticsSinkNode.cpp`.
- Observed: `sar_benchmark.cpp` is a large multi-concern file (graph execution, baseline execution, parity checks, telemetry serialization, policy assertions, trace schema output).
- Observed: Dual behavior in backprojection (native kernel path + simulated path) plus payload-store hooks increases branching in `SarBackprojectionTransformAccelNode.cpp`.
- Observed: Resolver behavior is split across parser validation, JSON mappings, plugin availability, and test-time temporary mapping injections.

## 8. Blockers for `AccelControlToken<SarSidecar>`

- Observed: Upstream range/source stages are message-contract based, not token-contract based.
- Observed: Merge/diagnostics consume status messages instead of token sidecar directly.
- Observed: Materialized image path relies on global side-channel payload store instead of edge-carried payload.
- Inferred: Converging to exactly one canonical SAR GPU flow requires collapsing mixed contract boundaries and side-channel dependencies.
- Unknown: Desired canonical home of SAR-specific sidecar/type aliases (remain SAR-local vs broader shared accel-token namespace).

## 9. Existing External Comparison/Baseline Hooks

- Observed: Deterministic baseline execution and graph-vs-baseline parity are implemented in `examples/SAR/src/sar_benchmark.cpp` and `examples/SAR/test/test_sar_baseline_compare.cpp`.
- Observed: Benchmark trace schema and required diagnostics/telemetry fields are enforced by `examples/SAR/test/test_sar_trace_schema.cpp`.
- Observed: Gotcha dataset adapter/replay path exists with CI-safe fixture and explicit external-data gating (`GRAPHX_SAR_ALLOW_EXTERNAL_DATA`, `allow_external_fixture`) in `examples/SAR/src/GotchaReplaySourceNode.cpp` and `examples/SAR/test/test_gotcha_dataset_adapter.cpp`.
- Observed: Manual external topology scaffold exists (`examples/SAR/config/sar_gotcha_external_manual.json`).
- Inferred: External comparison hooks are present and active, but mostly layered in benchmark/test/example scaffolding rather than core libgraph/libgpu contracts.

