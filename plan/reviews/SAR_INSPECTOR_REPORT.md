# SAR Inspector Report (Current Repository State)

Scope: Current repository state only. No redesign, no implementation.
Role contract source: `plan/agents/GRAPHX_SAR_AGENT_ROLES.md`.

## 1. Current Type Model

- Observed: Canonical SAR accel token alias exists: `SarAccelControlToken = AccelControlToken<SarSidecar>` in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `SarSidecar` is explicit identity/metadata carrier (sequence, batch, aperture, pulse range, stream, tile, backend, marker, payload size, queue IDs, stage timings, merge/materialization telemetry) in `examples/SAR/include/sar/SarMessages.hpp`.
- Observed: `AccelControlToken<SidecarT>` carries sidecar plus accel transport/control fields (`lease`, `device_view`, `host_view`, `transfer_ticket`, `kernel_ticket`, presence flags).
- Observed: Legacy SAR payload/status message structs targeted by PR5 are no longer defined in `examples/SAR/include/sar/SarMessages.hpp`:
  - `SarPulseBlockMessage`
  - `SarMergeStatusMessage`
  - `SarDiagnosticsMessage`
- Observed: Diagnostics reporting now uses `SarDiagnosticsSnapshot`, which stores canonical sidecar-derived reporting state without reintroducing a legacy message edge contract.
- Observed: Additional envelope/gpu structs remain (`SarMessageEnvelope`, `SarBufferDescriptor`, `SarGpuMetadata`, `SarDispatchMetadata`).
- Inferred: The maintained SAR runtime type surface is now canonical-token-first through merge/diagnostics, with legacy SAR message names retained only in parser/test/document guardrail contexts.

## 2. Current Node Model

- Observed: Definitive JSON topology in `examples/SAR/config/sar_stripmap_definitive.json` wires:
  `SyntheticApertureIqSourceNode -> RangeWindowNode -> RangeCompressionNode -> AzimuthTileSplitNode -> H2DAsyncNode -> SarBackprojectionTransformNode -> D2HAsyncNode -> ImageTileMergeNode -> SarDiagnosticsSinkNode`.
- Observed: Definitive stage contracts are tokenized end-to-end from source through diagnostics boundary:
  - `SyntheticApertureIqSourceNode`, `RangeWindowNode`, `RangeCompressionNode`, and `AzimuthTileSplitNode` use `SarAccelControlToken`.
  - `H2DAsyncAccelNode`, `SarBackprojectionTransformAccelNode`, and `D2HAsyncAccelNode` operate on `SarAccelControlToken`.
  - `ImageTileMergeNode` consumes token and emits token.
  - `SarDiagnosticsSinkNode` consumes token, stores canonical last-token state, and exposes `SarDiagnosticsSnapshot` reporting state.
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
- Observed: Merge emits token output and records merge-stage status/metrics into token sidecar fields rather than a primary-path status-message boundary.
- Observed: Token sidecar timings are accumulated across stages; merge and diagnostics preserve/report these fields from token sidecar data.
- Observed: Materialized-image capture now derives deterministic output directly from token-carried sidecar fields plus kernel-ticket validity in `examples/SAR/src/SarMaterializedImageSinkNode.cpp`.
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

- Observed: Definitive maintained path is tokenized through merge/diagnostics boundary.
- Observed: Global side-channel payload store has been removed from the primary path; materialization now uses explicit token-carried contract data.
- Observed: The PR5 legacy SAR message abstractions have been removed from the definitive runtime path and shared SAR type surface.
- Inferred: Remaining accel-token architecture gaps are no longer centered on the removed PR5 message abstractions; remaining questions are broader cleanup/documentation concerns outside the primary runtime contract.
- Unknown: Whether side-channel materialization path is considered acceptable runtime architecture or temporary instrumentation scaffolding.

## 6. Obsolete Abstractions

- Observed: Legacy SAR payload contract names are intentionally retained in parser as rejection literals (guardrails), not runtime contracts.
- Observed: Deprecated config files remain (`sar_stripmap_definitive_nonmetal.json`, `sar_stripmap_definitive_metal.json`) with deprecation notices.
- Observed: PR5 removed the legacy SAR message families from the shared SAR runtime type model and diagnostics sink API (`SarPulseBlockMessage`, `SarMergeStatusMessage`, `SarDiagnosticsMessage`).
- Observed: Legacy SAR message names still appear in parser rejection logic and negative-validation tests as string-only guardrails.
- Inferred: Some obsolete vocabulary is intentionally retained for guardrails/tests/document continuity, but not as active runtime type or edge-contract direction.

## 7. Complexity Hotspots

- Observed: Duplicate local `ElapsedUs` helper appears across many SAR source files:
  `AzimuthTileSplitNode.cpp`, `H2DAsyncAccelNode.cpp`, `SarBackprojectionTransformAccelNode.cpp`, `D2HAsyncAccelNode.cpp`, `ImageTileMergeNode.cpp`, `RangeWindowNode.cpp`, `RangeCompressionNode.cpp`, `SarDiagnosticsSinkNode.cpp`.
- Observed: `sar_benchmark.cpp` is a large multi-concern file (graph execution, baseline execution, parity checks, telemetry serialization, policy assertions, trace schema output).
- Observed: Dual behavior in backprojection (native kernel path + simulated path) plus payload-store hooks increases branching in `SarBackprojectionTransformAccelNode.cpp`.
- Observed: Resolver behavior is split across parser validation, JSON mappings, plugin availability, and test-time temporary mapping injections.

## 8. Blockers for `AccelControlToken<SarSidecar>`

- Observed: Source, DSP, merge, and diagnostics boundaries are tokenized in the maintained definitive path.
- Observed: Materialized image path no longer relies on global side-channel payload store in the primary path.
- Observed: PR5 removed the legacy message type surfaces that previously remained in merge/diagnostics reporting APIs.
- Inferred: Remaining blockers are no longer about primary-path message abstractions; they are limited to cleanup of stale documentation and any future narrowing of non-message helper structs if desired.
- Unknown: Desired canonical home of SAR-specific sidecar/type aliases (remain SAR-local vs broader shared accel-token namespace).

## 9. Existing External Comparison/Baseline Hooks

- Observed: Deterministic baseline execution and graph-vs-baseline parity are implemented in `examples/SAR/src/sar_benchmark.cpp` and `examples/SAR/test/test_sar_baseline_compare.cpp`.
- Observed: Benchmark trace schema and required diagnostics/telemetry fields are enforced by `examples/SAR/test/test_sar_trace_schema.cpp`.
- Observed: Gotcha dataset adapter/replay path exists with CI-safe fixture and explicit external-data gating (`GRAPHX_SAR_ALLOW_EXTERNAL_DATA`, `allow_external_fixture`) in `examples/SAR/src/GotchaReplaySourceNode.cpp` and `examples/SAR/test/test_gotcha_dataset_adapter.cpp`.
- Observed: Manual external topology scaffold exists (`examples/SAR/config/sar_gotcha_external_manual.json`).
- Inferred: External comparison hooks are present and active, but mostly layered in benchmark/test/example scaffolding rather than core libgraph/libgpu contracts.

